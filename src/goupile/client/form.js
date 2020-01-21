// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

let form_executor = new function() {
    let self = this;

    let block_go = false;

    let current_asset = {};
    let current_records = new BTree;
    let page_states = {};

    let show_complete = true;

    let select_many = false;
    let select_columns = new Set;

    this.route = async function(asset, url) {
        let parts = url.pathname.substr(asset.url.length).split('/');
        let id = parts[0] || null;

        if (asset !== current_asset || !current_records.size || id != null) {
            current_asset = asset;
            current_records.clear();
            page_states = {};

            if (id === 'many') {
                select_many = true;
            } else if (id === 'new' || id == null) {
                let record = virt_data.create(current_asset.form.key);
                current_records.set(record.id, record);

                select_many = false;
            } else if (id != null) {
                let record = await virt_data.load(current_asset.form.key, id) || virt_data.create(current_asset.form.key);
                current_records.set(record.id, record);

                select_many = false;
            }
        }
    };

    this.makeURL = function() {
        let url;
        if (select_many) {
            url = `${current_asset.url}many`;
        } else if (!current_records.size) {
            url = current_asset.url;
        } else {
            let record = current_records.values().next().value;

            url = `${current_asset.url}${record.sequence != null ? record.id : 'new'}`;
        }

        return util.pasteURL(url, app.route);
    };

    this.runPage = function(code, panel_el) {
        let func = Function('util', 'data', 'go', 'form', 'page', 'route', 'scratch', code);

        if (!select_many || select_columns.size) {
            try {
                // We don't want go() to be fired when a script is opened or changed in the editor,
                // because then we wouldn't be able to come back to the script to fix the code.
                block_go = true;

                render(html`
                    <div class="af_page">${util.map(current_records.values(), record => {
                        let state = page_states[record.id];
                        if (!state) {
                            state = new PageState;
                            page_states[record.id] = state;
                        }

                        // Pages need to update themselves without doing a full render
                        let el = document.createElement('div');
                        if (select_many)
                            el.className = 'af_entry';

                        if (select_many) {
                            runPageMany(state, func, record, select_columns, el);
                        } else {
                            runPage(state, func, record, el);
                        }

                        return el;
                    })}</div>
                `, panel_el);
            } finally {
                block_go = false;
            }
        } else {
            render(html`<div class="af_page">Aucune colonne sélectionnée</div>`, panel_el);
        }
    };

    function runPageMany(state, func, record, columns, el) {
        let page = new Page(current_asset.page.key);
        let builder = new PageBuilder(state, page);

        builder.decodeKey = decodeKey;
        builder.setValue = (key, value) => setValue(record, key, value);
        builder.getValue = (key, default_value) => getValue(record, key, default_value);
        builder.submitHandler = async (complete) => {
            await saveRecord(record, page, complete);
            state.changed = false;

            goupile.go();
        };
        builder.changeHandler = () => runPageMany(...arguments);

        // Build it!
        builder.pushOptions({compact: true});
        func(util, app.data, handleGo, builder, builder, {}, state.scratch);

        render(html`
            <div class="af_actions">
                <button class="af_button" ?disabled=${!state.changed}
                        @click=${builder.save}>Enregistrer</button>
            </div>

            ${page.widgets.map(intf => {
                let visible = intf.key && columns.has(intf.key.toString());
                return visible ? intf.render() : '';
            })}
        `, el);
    }

    function runPage(state, func, record, el) {
        let page = new Page(current_asset.page.key);
        let builder = new PageBuilder(state, page);

        builder.decodeKey = decodeKey;
        builder.setValue = (key, value) => setValue(record, key, value);
        builder.getValue = (key, default_value) => getValue(record, key, default_value);
        builder.submitHandler = async (complete) => {
            await saveRecord(record, page, complete);
            state.changed = false;

            goupile.go();
        };
        builder.changeHandler = () => runPage(...arguments);

        // Build it!
        builder.errorList();
        func(util, app.data, handleGo, builder, builder, app.route, state.scratch);
        builder.errorList();
        window.history.replaceState(null, null, self.makeURL());

        let show_new = (record.sequence != null);
        let enable_save = state.changed;
        let enable_validate = !state.changed && !page.errors.length &&
                              record.complete[page.key] === false;

        render(html`
            <div class="af_path">${current_asset.form.pages.map(page2 => {
                let complete = record.complete[page2.key];

                let cls = '';
                if (page2.key === page.key)
                    cls += ' active';
                if (complete == null) {
                    // Leave as is
                } else if (complete) {
                    cls += ' complete';
                } else {
                    cls += ' partial';
                }

                return html`<a class=${cls} href=${makeLink(current_asset.form.key, page2.key, record.id)}>${page2.label}</a>`;
            })}</div>

            <div class="af_actions sticky">
                ${record.sequence != null ? html`
                    <a href="#" @click=${e => { handleNewClick(e, state.changed); e.preventDefault(); }}>x</a>
                    <p>ID n°${record.sequence}</p>
                ` : ''}
                ${record.sequence == null ? html`<p>Nouvel ID</p>` : ''}

                <button class="af_button" ?disabled=${!enable_save}
                        @click=${builder.save}>Enregistrer</button>
                <button class="af_button" ?disabled=${!enable_validate}
                        @click=${e => showValidateDialog(e, builder.submit)}>Valider</button>
            </div>

            <br/>
            ${page.render()}
        `, el);
    }

    // Avoid async here, because it may fail (see block_go) and the caller may need
    // to catch that synchronously.
    function handleGo(url = null, push_history = true) {
        if (block_go) {
            throw new Error(`A navigation function (e.g. go()) has been interrupted.
Navigation functions should only be called in reaction to user events, such as button clicks.`);
        }

        goupile.go(url, push_history);
    }

    function decodeKey(key) {
        if (!key)
            throw new Error('Empty keys are not allowed');
        if (!key.match(/^[a-zA-Z_][a-zA-Z0-9_]*$/))
            throw new Error('Allowed key characters: a-z, _ and 0-9 (not as first character)');

        key = {
            variable: key,
            toString: () => key.variable
        };

        return key;
    };

    function setValue(record, key, value) {
        record.values[key] = value;
    }

    function getValue(record, key, default_value) {
        if (!record.values.hasOwnProperty(key)) {
            record.values[key] = default_value;
            return default_value;
        }

        return record.values[key];
    }

    async function saveRecord(record, page, complete = false) {
        record.complete[page.key] = complete;

        let entry = new log.Entry();

        entry.progress('Enregistrement en cours');
        try {
            let record2 = await virt_data.save(record, page.variables);
            entry.success('Données enregistrées !');

            if (current_records.has(record2.id))
                current_records.set(record2.id, record2);
        } catch (err) {
            entry.error(`Échec de l\'enregistrement : ${err.message}`);
        }
    }

    function handleNewClick(e, confirm) {
        if (confirm) {
            goupile.popup(e, page => {
                page.output('Cette action entraînera la perte des modifications en cours, êtes-vous sûr(e) ?');

                page.submitHandler = () => {
                    page.close();
                    goupile.go(makeLink(current_asset.form.key, current_asset.page.key));
                };
                page.buttons(page.buttons.std.ok_cancel('Fermer l\'enregistrement'));
            })
        } else {
            goupile.go(makeLink(current_asset.form.key, current_asset.page.key));
        }
    }

    function showValidateDialog(e, submit_func) {
        goupile.popup(e, page => {
            page.output('Confirmez-vous la validation de cette page ?');

            page.submitHandler = () => {
                page.close();
                submit_func(true);
            };
            page.buttons(page.buttons.std.ok_cancel('Valider'));
        });
    }

    this.runStatus = async function() {
        let records = await virt_data.loadAll(current_asset.form.key);
        renderStatus(records);
    };

    function renderStatus(records) {
        let pages = current_asset.form.pages;

        let complete_set = new Set;
        for (let record of records) {
            // We can't compute this at save time because the set of pages may change anytime
            if (pages.every(page => record.complete[page.key]))
                complete_set.add(record.id);
        }

        render(html`
            <div class="gp_toolbar">
                <div style="flex: 1;"></div>
                <p>${records.length} ${records.length > 1 ? 'enregistrements' : 'enregistrement'}
                   dont ${complete_set.size} ${complete_set.size > 1 ? 'complets' : 'complet'}</p>
                <div style="flex: 1;"></div>
                <button class=${show_complete ? 'active' : ''}
                        @click=${toggleShowComplete}>Afficher les enregistrements complets</button>
            </div>

            <table class="st_table">
                <thead>
                    <tr>
                        <th class="id">ID</th>
                        ${pages.map(page => html`<th>${page.key}</th>`)}
                    </tr>
                </thead>

                <tbody>
                    ${!records.length ?
                        html`<tr><td style="text-align: left;"
                                     colspan=${1 + Math.max(1, pages.length)}>Aucune donnée à afficher</td></tr>` : ''}
                    ${records.map(record => {
                        if (show_complete || !complete_set.has(record.id)) {
                            return html`
                                <tr>
                                    <td class="id">${record.sequence}</td>
                                    ${pages.map(page => {
                                        let complete = record.complete[page.key];

                                        if (complete == null) {
                                            return html`<td><a href=${makeLink(current_asset.form.key, page.key, record.id)}>Non rempli</a></td>`;
                                        } else if (complete) {
                                            return html`<td class="complete"><a href=${makeLink(current_asset.form.key, page.key, record.id)}>Complet</a></td>`;
                                        } else {
                                            return html`<td class="partial"><a href=${makeLink(current_asset.form.key, page.key, record.id)}>Partiel</a></td>`;
                                        }
                                    })}
                                </tr>
                            `;
                        } else {
                            return '';
                        }
                    })}
                </tbody>
            </table>
        `, document.querySelector('#dev_status'));
    }

    function toggleShowComplete() {
        show_complete = !show_complete;
        goupile.go();
    }

    this.runData = async function() {
        let records = await virt_data.loadAll(current_asset.form.key);
        let variables = await virt_data.listVariables(current_asset.form.key);
        let columns = orderColumns(current_asset.form.pages, variables);

        renderRecords(records, columns);
    };

    function renderRecords(records, columns) {
        let empty_msg;
        if (!records.length) {
            empty_msg = 'Aucune donnée à afficher';
        } else if (!columns.length) {
            empty_msg = 'Impossible d\'afficher les données (colonnes inconnues)';
            records = [];
        }

        let count1 = 0;
        let count0 = 0;
        if (select_many) {
            for (let record of records) {
                if (current_records.has(record.id)) {
                    count1++;
                } else {
                    count0++;
                }
            }
        }

        render(html`
            <div class="gp_toolbar">
                <button class=${select_many ? 'active' : ''} @click=${e => toggleSelectionMode()}>Sélection multiple</button>
                <div style="flex: 1;"></div>
                <div class="gp_dropdown right">${renderExportMenu()}</div>
            </div>

            <table class="rec_table" style=${`min-width: ${30 + 60 * columns.length}px`}>
                <thead>
                    <tr>
                        <th class="actions">
                            ${select_many ?
                                html`<input type="checkbox" .checked=${count1 && !count0}
                                            .indeterminate=${count1 && count0}
                                            @change=${e => toggleAllRecords(records, e.target.checked)} />` : ''}
                        </th>
                        <th class="id">ID</th>

                        ${!columns.length ? html`<th>&nbsp;</th>` : ''}
                        ${!select_many ? columns.map(col => html`<th title=${col.key}>${col.key}</th>`) : ''}
                        ${select_many ? columns.map(col =>
                            html`<th title=${col.key}><input type="checkbox" .checked=${select_columns.has(col.key)}
                                                             @change=${e => toggleColumn(col.key)} />${col.key}</th>`) : ''}
                    </tr>
                </thead>

                <tbody>
                    ${empty_msg ?
                        html`<tr><td colspan=${2 + Math.max(1, columns.length)}>${empty_msg}</td></tr>` : ''}
                    ${records.map(record => html`<tr class=${current_records.has(record.id) ? 'selected' : ''}>
                        ${!select_many ? html`<th><a href="#" @click=${e => { handleEditClick(record); e.preventDefault(); }}>🔍\uFE0E</a>
                                                  <a href="#" @click=${e => { showDeleteDialog(e, record); e.preventDefault(); }}>✕</a></th>` : ''}
                        ${select_many ? html`<th><input type="checkbox" .checked=${current_records.has(record.id)}
                                                        @click=${e => handleEditClick(record)} /></th>` : ''}

                        <td class="id">${record.sequence}</td>
                        ${columns.map(col => {
                            let value = record.values[col.key];

                            if (value == null) {
                                return html`<td class="missing" title="Donnée manquante">NA</td>`;
                            } else if (Array.isArray(value)) {
                                let text = value.join('|');
                                return html`<td title=${text}>${text}</td>`;
                            } else if (typeof value === 'number') {
                                return html`<td class="number" title=${value}>${value}</td>`;
                            } else {
                                return html`<td title=${value}>${value}</td>`;
                            }
                        })}
                    </tr>`)}
                </tbody>
            </table>
        `, document.querySelector('#dev_data'));
    }

    function renderExportMenu() {
        return html`
            <button>Export</button>
            <div>
                <button @click=${e => exportSheets(current_asset.form, 'xlsx')}>Excel</button>
                <button @click=${e => exportSheets(current_asset.form, 'csv')}>CSV</button>
            </div>
        `;
    }

    function toggleSelectionMode() {
        select_many = !select_many;

        let record0 = current_records.size ? current_records.values().next().value : null;

        if (select_many) {
            select_columns.clear();
            if (record0 && record0.sequence == null)
                current_records.clear();
        } else if (record0) {
            current_records.clear();
            current_records.set(record0.id, record0);
        }

        goupile.go();
    }

    function toggleAllRecords(records, enable) {
        current_records.clear();

        if (enable) {
            for (let record of records)
                current_records.set(record.id, record);
        }

        goupile.go();
    }

    function toggleColumn(key) {
        if (select_columns.has(key)) {
            select_columns.delete(key);
        } else {
            select_columns.add(key);
        }

        goupile.go();
    }

    async function exportSheets(form, format = 'xlsx') {
        if (typeof XSLX === 'undefined')
            await util.loadScript(`${env.base_url}static/xlsx.core.min.js`);

        let records = await virt_data.loadAll(form.key);
        let variables = await virt_data.listVariables(form.key);
        let columns = orderColumns(form.pages, variables);

        if (!columns.length) {
            log.error('Impossible d\'exporter pour le moment (colonnes inconnues)');
            return;
        }

        // Worksheet
        let ws = XLSX.utils.aoa_to_sheet([columns.map(col => col.key)]);
        for (let record of records) {
            let values = columns.map(col => record.values[col.key]);
            XLSX.utils.sheet_add_aoa(ws, [values], {origin: -1});
        }

        // Workbook
        let wb = XLSX.utils.book_new();
        let ws_name = `${env.app_key}_${dates.today()}`;
        XLSX.utils.book_append_sheet(wb, ws, ws_name);

        let filename = `export_${ws_name}.${format}`;
        switch (format) {
            case 'xlsx': { XLSX.writeFile(wb, filename); } break;
            case 'csv': { XLSX.writeFile(wb, filename, {FS: ';'}); } break;
        }
    }

    // XXX: Improve performance and ordering of variables in different pages
    function orderColumns(pages, variables) {
        variables = variables.slice();
        variables.sort((variable1, variable2) => util.compareValues(variable1.key, variable2.key));

        let variables_map = util.mapArray(variables, variable => variable.key);

        let columns = [];
        for (let page of pages) {
            let first_set = new Set;
            let sets_map = {};
            for (let variable of variables) {
                if (variable.page === page.key) {
                    if (variable.before == null) {
                        first_set.add(variable.key);
                    } else {
                        let set_ptr = sets_map[variable.before];
                        if (!set_ptr) {
                            set_ptr = new Set;
                            sets_map[variable.before] = set_ptr;
                        }

                        set_ptr.add(variable.key);
                    }
                }
            }

            let next_sets = [first_set];
            let next_set_idx = 0;
            while (next_set_idx < next_sets.length) {
                let set_ptr = next_sets[next_set_idx++];
                let set_start_idx = columns.length;

                while (set_ptr.size) {
                    let frag_start_idx = columns.length;

                    for (let key of set_ptr) {
                        let variable = variables_map[key];

                        if (!set_ptr.has(variable.after)) {
                            let col = {
                                page: variable.page,
                                key: key,
                                type: variable.type
                            };
                            columns.push(col);
                        }
                    }

                    reverseLastColumns(columns, frag_start_idx);

                    // Avoid infinite loop that may happen in rare cases
                    if (columns.length === frag_start_idx) {
                        let use_key = set_ptr.values().next().value;

                        let col = {
                            page: variables_map[use_key].page,
                            key: use_key,
                            type: variables_map[use_key].type
                        };
                        columns.push(col);
                    }

                    for (let i = frag_start_idx; i < columns.length; i++) {
                        let key = columns[i].key;

                        let next_set = sets_map[key];
                        if (next_set) {
                            next_sets.push(next_set);
                            delete sets_map[key];
                        }

                        delete variables_map[key];
                        set_ptr.delete(key);
                    }
                }

                reverseLastColumns(columns, set_start_idx);
            }
        }

        // Remaining variables (probably from old forms, or discarded pages)
        for (let key in variables_map) {
            let col = {
                page: variables_map[key].page,
                key: key,
                type: variables_map[key].type
            }
            columns.push(col);
        }

        return columns;
    }

    function reverseLastColumns(columns, start_idx) {
        for (let i = 0; i < (columns.length - start_idx) / 2; i++) {
            let tmp = columns[start_idx + i];
            columns[start_idx + i] = columns[columns.length - i - 1];
            columns[columns.length - i - 1] = tmp;
        }
    }

    function handleEditClick(record) {
        let show_overview = !current_records.size;

        if (!current_records.has(record.id)) {
            if (!select_many)
                current_records.clear();
            current_records.set(record.id, record);
        } else {
            current_records.delete(record.id);
        }

        if (show_overview) {
            goupile.toggleOverview(true);
        } else {
            goupile.go();
        }
    }

    function showDeleteDialog(e, record) {
        goupile.popup(e, page => {
            page.output('Voulez-vous vraiment supprimer cet enregistrement ?');

            page.submitHandler = async () => {
                page.close();

                await virt_data.delete(record.table, record.id);
                current_records.delete(record.id, record);

                goupile.go();
            };
            page.buttons(page.buttons.std.ok_cancel('Supprimer'));
        });
    }

    this.runDescribe = async function() {
        if (typeof Chart === 'undefined')
            await util.loadScript(`${env.base_url}static/chart.bundle.min.js`);

        render(html`
            <div class="gp_toolbar"></div>
            <div class="gp_wip">Graphiques non disponibles</div>
        `, document.querySelector('#dev_describe'));
    };

    function makeLink(form_key, page_key, id = null) {
        return `${env.base_url}app/${form_key}/${page_key}/${id || 'new'}`;
    }
};
