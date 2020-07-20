// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

let form_exec = new function() {
    let self = this;

    let current_asset = {};
    let current_records = new BTree;
    let page_states = {};

    let show_complete = true;

    let multi_mode = false;
    let multi_columns = new Set;

    this.route = async function(asset, url) {
        let parts = url.pathname.substr(asset.url.length).split('/');
        let what = parts[0] || null;

        if (parts.length !== 1)
            throw new Error(`Adresse incorrecte '${url.pathname}'`);
        if (what && what !== 'multi' && !what.match(/^[A-Z0-9]{26}(@[0-9]+)?$/))
            throw new Error(`Adresse incorrecte '${url.pathname}'`);

        current_asset = asset;

        if (current_records.size) {
            let record0 = current_records.first();
            if (record0.table !== asset.form.key)
                current_records.clear();
        }

        if (what === 'multi') {
            multi_mode = true;
        } else if (what != null) {
            let [id, version] = what.split('@');
            if (version != null)
                version = parseInt(version, 10);

            let record = current_records.get(id);
            if (record) {
                if (version != null) {
                    if (version !== record.version)
                        record = null;
                } else {
                    if (record.version !== record.versions.length - 1)
                        record = null;
                }
            }
            if (!record) {
                record = await vrec.load(asset.form.key, id, version) ||
                               vrec.create(asset.form.key);
                delete page_states[id];

                if (version != null && record.version !== version)
                    log.error(`La fiche version @${version} n'existe pas\nVersion chargée : @${record.version}`);
            }

            current_records.clear();
            current_records.set(record.id, record);

            multi_mode = false;
        } else {
            let record = vrec.create(asset.form.key);

            current_records.clear();
            current_records.set(record.id, record);

            multi_mode = false;
        }

        let new_states = {};
        for (let id of current_records.keys())
            new_states[id] = page_states[id];
        page_states = new_states;
    };

    this.runPage = function(code, panel_el) {
        let func = Function('util', 'shared', 'go', 'form', 'page',
                            'values', 'variables', 'route', 'scratch', code);

        if (multi_mode) {
            if (current_records.size && multi_columns.size) {
                render(html`
                    <div class="fm_page">${util.map(current_records.values(), record => {
                        // Each entry needs to update itself without doing a full render
                        let el = document.createElement('div');
                        el.className = 'fm_entry';

                        runPageMulti(func, record, multi_columns, el);

                        return el;
                    })}</div>
                `, panel_el);
            } else if (!current_records.size) {
                render(html`<div class="fm_page">Aucun enregistrement sélectionné</div>`, panel_el);
            } else {
                render(html`<div class="fm_page">Aucune colonne sélectionnée</div>`, panel_el);
            }
        } else {
            if (!current_records.size) {
                let record = vrec.create(current_asset.form.key);
                current_records.set(record.id, record);
            }

            let record = current_records.first();
            runPage(func, record, panel_el);
        }
    };

    function runPageMulti(func, record, columns, el) {
        let state = page_states[record.id];
        if (!state) {
            state = new PageState;
            page_states[record.id] = state;
        }

        let page = new Page(current_asset.page.key);
        let builder = new PageBuilder(state, page);

        builder.decodeKey = decodeKey;
        builder.setValue = (key, value) => setValue(record, key, value);
        builder.getValue = (key, default_value) => getValue(record, key, default_value);
        builder.submitHandler = async () => {
            await saveRecord(record, page);
            state.changed = false;

            await goupile.go();
        };
        builder.changeHandler = () => runPageMulti(...arguments);

        // Build it!
        builder.pushOptions({compact: true});
        func(util, app.shared, nav.go, builder, builder,
             state.values, page.variables, {}, state.scratch);

        render(html`
            <button type="button" class="af_button" style="float: right;"
                    ?disabled=${builder.hasErrors() || !state.changed}
                    @click=${builder.submit}>Enregistrer</button>

            ${page.widgets.map(intf => {
                let visible = intf.key && columns.has(intf.key.toString());
                return visible ? intf.render() : '';
            })}
        `, el);

        window.history.replaceState(null, null, makeURL());
    }

    function runPage(func, record, el) {
        let state = page_states[record.id];
        if (!state) {
            state = new PageState;
            page_states[record.id] = state;
        }

        let page = new Page(current_asset.page.key);
        let builder = new PageBuilder(state, page);

        builder.decodeKey = decodeKey;
        builder.setValue = (key, value) => setValue(record, key, value);
        builder.getValue = (key, default_value) => getValue(record, key, default_value);
        builder.submitHandler = async () => {
            await saveRecord(record, page);
            state.changed = false;

            await goupile.go();
        };
        builder.changeHandler = () => runPage(...arguments);

        // Build it!
        func(util, app.shared, nav.go, builder, builder,
             state.values, page.variables, app.route, state.scratch);
        builder.errorList();

        let show_actions = current_asset.form.options.actions && page.variables.length;
        let enable_save = !builder.hasErrors() && state.changed;
        let enable_validate = !builder.hasErrors() && !state.changed &&
                              record.complete[page.key] === false;

        render(html`
            <div class="fm_form">
                <div class="fm_path">${current_asset.form.pages.map(page2 => {
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

                    return html`<a class=${cls} href=${makeLink(current_asset.form.key, page2.key, record)}>${page2.label}</a>`;
                })}</div>

                ${show_actions ? html`
                    <div class="fm_id">
                        ${record.mtime == null ? html`Nouvel enregistrement` : ''}
                        ${record.mtime != null && record.sequence == null ? html`Enregistrement local` : ''}
                        ${record.mtime != null && record.sequence != null ? html`Enregistrement n°${record.sequence}` : ''}

                        ${record.mtime != null ? html`(<a @click=${e => showTrailDialog(e, record)}>trail</a>)` : ''}
                    </div>
                ` : ''}

                <div class="fm_page">${page.render()}</div>

                ${show_actions ? html`
                    <div class="af_actions">
                        <button type="button" class="af_button" ?disabled=${!enable_save}
                                @click=${builder.submit}>Enregistrer</button>
                        ${current_asset.form.options.validate ?
                            html`<button type="button" class="af_button" ?disabled=${!enable_validate}
                                         @click=${e => showValidateDialog(e, builder.submit)}>Valider</button>`: ''}
                        <hr/>
                        <button type="button" class="af_button" ?disabled=${!state.changed && record.mtime == null}
                                @click=${e => handleNewClick(e, state.changed)}>Fermer</button>
                    </div>
                `: ''}
            </div>
        `, el);

        window.history.replaceState(null, null, makeURL());
    }

    function showTrailDialog(e, record) {
        goupile.popup(e, null, (page, close) => {
            page.output(html`
                <table class="tr_table">
                    <thead>
                        <tr>
                            <th></th>
                            <th>Modification</th>
                            <th>Utilisateur</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${util.mapRange(0, record.versions.length, idx => {
                            let version = record.versions[record.versions.length - idx - 1];
                            let url = makeLink(current_asset.form.key, current_asset.page.key, record, version.version);

                            return html`
                                <tr>
                                    <td><a href=${url}>🔍\uFE0E</a></td>
                                    <td>${version.mtime}</td>
                                    <td>${version.username || '(local)'}</td>
                                </tr>
                            `;
                        })}
                    </tbody>
                </table>
            `);
        });
    }

    function makeURL() {
        let url;
        if (multi_mode) {
            url = `${current_asset.url}multi`;
        } else if (!current_records.size) {
            url = current_asset.url;
        } else {
            let record = current_records.first();

            url = current_asset.url;
            if (record.mtime != null) {
                url += record.id;
                if (record.version !== record.versions.length - 1)
                    url += `@${record.version}`;
            }
        }

        return util.pasteURL(url, app.route);
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

    async function saveRecord(record, page) {
        let entry = new log.Entry();

        entry.progress('Enregistrement en cours');
        try {
            let record2 = await vrec.save(record, page.key, page.variables, goupile.getUserName());
            entry.success('Données enregistrées');

            if (current_records.has(record2.id))
                current_records.set(record2.id, record2);
        } catch (err) {
            entry.error(`Échec de l\'enregistrement : ${err.message}`);
        }
    }

    function handleNewClick(e, confirm) {
        if (confirm) {
            goupile.popup(e, 'Fermer l\'enregistrement', (page, close) => {
                page.output('Cette action entraînera la perte des modifications en cours, êtes-vous sûr(e) ?');

                page.submitHandler = () => {
                    close();
                    goupile.go(makeLink(current_asset.form.key, current_asset.page.key, null));
                };
            })
        } else {
            goupile.go(makeLink(current_asset.form.key, current_asset.page.key, null));
        }
    }

    function showValidateDialog(e, submit_func) {
        goupile.popup(e, 'Valider', (page, close) => {
            page.output('Confirmez-vous la validation de cette page ?');

            page.submitHandler = () => {
                close();
                submit_func(true);
            };
        });
    }

    this.runStatus = async function() {
        let records = await vrec.loadAll(current_asset.form.key);
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
                <button type="button" class=${show_complete ? 'active' : ''}
                        @click=${toggleShowComplete}>Afficher les enregistrements complets</button>
            </div>

            <table class="st_table">
                <colgroup>
                    <col style="width: 3em;"/>
                    <col style="width: 60px;"/>
                    ${pages.map(col => html`<col/>`)}
                </colgroup>

                <thead>
                    <tr>
                        <th class="actions"></th>
                        <th class="id">ID</th>
                        ${pages.map(page => html`<th>${page.label}</th>`)}
                    </tr>
                </thead>

                <tbody>
                    ${!records.length ?
                        html`<tr><td style="text-align: left;"
                                     colspan=${2 + Math.max(1, pages.length)}>Aucune donnée à afficher</td></tr>` : ''}
                    ${records.map(record => {
                        if (show_complete || !complete_set.has(record.id)) {
                            return html`
                                <tr class=${current_records.has(record.id) ? 'selected' : ''}>
                                    <th>
                                        <a @click=${e => handleEditClick(record)}>🔍\uFE0E</a>
                                        <a @click=${e => showDeleteDialog(e, record)}>✕</a>
                                    </th>
                                    <td class="id">${record.sequence || 'local'}</td>

                                    ${pages.map(page => {
                                        let complete = record.complete[page.key];

                                        if (complete == null) {
                                            return html`<td class="none"><a href=${makeLink(current_asset.form.key, page.key, record)}>Non rempli</a></td>`;
                                        } else if (complete) {
                                            return html`<td class="complete"><a href=${makeLink(current_asset.form.key, page.key, record)}>Validé</a></td>`;
                                        } else {
                                            return html`<td class="partial"><a href=${makeLink(current_asset.form.key, page.key, record)}>Enregistré</a></td>`;
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
        let records = await vrec.loadAll(current_asset.form.key);
        let variables = await vrec.listVariables(current_asset.form.key);
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
        if (multi_mode) {
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
                <button type="button" class=${multi_mode ? 'active' : ''}
                        @click=${e => toggleSelectionMode()}>Sélection multiple</button>
                <div style="flex: 1;"></div>
                <div class="gp_dropdown right">${renderExportMenu()}</div>
            </div>

            <table class="rec_table" style=${`min-width: ${30 + 60 * columns.length}px`}>
                <colgroup>
                    <col style="width: 3em;"/>
                    <col style="width: 60px;"/>
                    ${!columns.length ? html`<col/>` : ''}
                    ${columns.map(col => html`<col/>`)}
                </colgroup>

                <thead>
                    ${columns.length ? html`
                        <tr>
                            <th colspan="2"></th>
                            ${util.mapRLE(columns, col => col.page, (page, offset, len) =>
                                html`<th class="rec_page" colspan=${len}>${page}</th>`)}
                        </tr>
                    ` : ''}
                    <tr>
                        <th class="actions">
                            ${multi_mode ?
                                html`<input type="checkbox" .checked=${count1 && !count0}
                                            .indeterminate=${count1 && count0}
                                            @change=${e => toggleAllRecords(records, e.target.checked)} />` : ''}
                        </th>
                        <th class="id">ID</th>

                        ${!columns.length ? html`<th>&nbsp;</th>` : ''}
                        ${!multi_mode ? columns.map(col => html`<th title=${col.key}>${col.key}</th>`) : ''}
                        ${multi_mode ? columns.map(col =>
                            html`<th title=${col.key}><input type="checkbox" .checked=${multi_columns.has(col.key)}
                                                             @change=${e => toggleColumn(col.key)} />${col.key}</th>`) : ''}
                    </tr>
                </thead>

                <tbody>
                    ${empty_msg ?
                        html`<tr><td colspan=${2 + Math.max(1, columns.length)}>${empty_msg}</td></tr>` : ''}
                    ${records.map(record => html`
                        <tr class=${current_records.has(record.id) ? 'selected' : ''}>
                            ${!multi_mode ? html`<th><a @click=${e => handleEditClick(record)}>🔍\uFE0E</a>
                                                      <a @click=${e => showDeleteDialog(e, record)}>✕</a></th>` : ''}
                            ${multi_mode ? html`<th><input type="checkbox" .checked=${current_records.has(record.id)}
                                                            @click=${e => handleEditClick(record)} /></th>` : ''}
                            <td class="id">${record.sequence || 'local'}</td>

                            ${columns.map(col => {
                                let value = record.values[col.key];

                                if (value == null) {
                                    return html`<td class="missing" title="Donnée manquante">MD</td>`;
                                } else if (Array.isArray(value)) {
                                    let text = value.join('|');
                                    return html`<td title=${text}>${text}</td>`;
                                } else if (typeof value === 'number') {
                                    return html`<td class="number" title=${value}>${value}</td>`;
                                } else {
                                    return html`<td title=${value}>${value}</td>`;
                                }
                            })}
                        </tr>
                    `)}
                </tbody>
            </table>
        `, document.querySelector('#dev_data'));
    }

    function renderExportMenu() {
        return html`
            <button type="button">Export</button>
            <div>
                <button type="button" @click=${e => exportSheets(current_asset.form, 'xlsx')}>Excel</button>
                <button type="button" @click=${e => exportSheets(current_asset.form, 'csv')}>CSV</button>
            </div>
        `;
    }

    function toggleSelectionMode() {
        multi_mode = !multi_mode;

        let record0 = current_records.size ? current_records.values().next().value : null;

        if (multi_mode) {
            multi_columns.clear();
            if (record0 && record0.mtime == null)
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
        if (multi_columns.has(key)) {
            multi_columns.delete(key);
        } else {
            multi_columns.add(key);
        }

        goupile.go();
    }

    async function exportSheets(form, format = 'xlsx') {
        if (typeof XSLX === 'undefined')
            await net.loadScript(`${env.base_url}static/xlsx.core.min.js`);

        let records = await vrec.loadAll(form.key);
        let variables = await vrec.listVariables(form.key);
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

    function orderColumns(pages, variables) {
        variables = variables.slice();
        variables.sort((variable1, variable2) => util.compareValues(variable1.key, variable2.key));

        let frags_map = {};
        for (let variable of variables) {
            let frag_variables = frags_map[variable.page];
            if (!frag_variables) {
                frag_variables = [];
                frags_map[variable.page] = frag_variables;
            }

            frag_variables.push(variable);
        }

        let columns = [];
        for (let page of pages) {
            let frag_variables = frags_map[page.key] || [];
            delete frags_map[page.key];

            let variables_map = util.mapArray(frag_variables, variable => variable.key);

            let first_set = new Set;
            let sets_map = {};
            for (let variable of frag_variables) {
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
                                page: page.label,
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
                            page: page.label,
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

            // Remaining page variables
            for (let key in variables_map) {
                let col = {
                    page: page.label,
                    key: key,
                    type: variables_map[key].type
                }
                columns.push(col);
            }
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
            if (!multi_mode)
                current_records.clear();
            current_records.set(record.id, record);
        } else {
            current_records.delete(record.id);

            if (!multi_mode && !current_records.size) {
                let record = vrec.create(current_asset.form.key);
                current_records.set(record.id, record);
            }
        }

        if (show_overview) {
            goupile.toggleOverview(true);
        } else {
            goupile.go();
        }
    }

    function showDeleteDialog(e, record) {
        goupile.popup(e, 'Supprimer', (page, close) => {
            page.output('Voulez-vous vraiment supprimer cet enregistrement ?');

            page.submitHandler = async () => {
                close();

                await vrec.delete(record.table, record.id);
                current_records.delete(record.id, record);

                goupile.go();
            };
        });
    }

    function makeLink(form_key, page_key, record = undefined, version = undefined) {
        let url = `${env.base_url}app/${form_key}/${page_key || form_key}/`;
        if (record && record.mtime != null)
            url += record.id + (version != null ? `@${version}` : '');
        return url;
    }
};