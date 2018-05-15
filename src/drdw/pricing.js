var pricing = {};
(function() {
    // URL settings (routing)
    var target_mode = 'table';
    var target_date = null;
    var target_ghm_root = null;
    var target_diff = null;

    var errors = new Set();

    var indexes_init = false;
    var indexes = [];
    var ghm_roots_init = false;
    var ghm_roots = [];
    var ghm_roots_map = {};
    var pricings_map = {};

    var chart = null;

    function run()
    {
        // Parse route (model: pricing/<mode>/[<diff>..]<date>/<ghm_root>)
        var parts = url_page.split('/');
        target_mode = parts[1] || target_mode;
        if (parts[2]) {
            var date_parts = parts[2].split('..', 2);
            if (date_parts.length === 2) {
                target_date = date_parts[1];
                target_diff = date_parts[0];
            } else {
                target_date = date_parts[0];
                target_diff = null;
            }
        }
        target_ghm_root = parts[3] || target_ghm_root;

        // Validate
        if (target_mode !== 'chart' && target_mode !== 'table')
            errors.add('Mode d\'affichage incorrect');
        var main_index = indexes.findIndex(function(info) { return info.begin_date === target_date; });
        if (target_date !== null && indexes.length && main_index < 0)
            errors.add('Date incorrecte');
        var diff_index = indexes.findIndex(function(info) { return info.begin_date === target_diff; });
        if (target_diff !== null && indexes.length && diff_index < 0)
            errors.add('Date de comparaison incorrecte');
        if (target_ghm_root !== null && ghm_roots.length) {
            if (!ghm_roots_map[target_ghm_root]) {
                errors.add('Racine de GHM inconnue');
            } else {
                if (!checkIndexGhmRoot(main_index, target_ghm_root))
                    errors.add('Cette racine n\'existe pas dans la version \'' + indexes[main_index].begin_date + '\'');
                if (!checkIndexGhmRoot(diff_index, target_ghm_root))
                    errors.add('Cette racine n\'existe pas dans la version \'' + indexes[diff_index].begin_date + '\'');
            }
        }

        // Redirection (stable URLs)
        if (target_date === null && indexes.length) {
            route({date: indexes[indexes.length - 1].begin_date});
            return;
        } else if (target_ghm_root === null && ghm_roots.length) {
            route({ghm_root: ghm_roots[0].ghm_root});
            return;
        }

        // Resources
        updateIndexes(run);
        updateGhmRoots(run);
        if (main_index >= 0 && !indexes[main_index].init) {
            markOutdated('#pricing_view', true);
            updatePriceMap(main_index, run);
        }
        if (diff_index >= 0 && !indexes[diff_index].init) {
            markOutdated('#pricing_view', true);
            updatePriceMap(diff_index, run);
        }

        // Refresh display
        document.querySelector('#pricing').classList.add('active');
        document.querySelector('#pricing_table').classList.toggle('active', target_mode === 'table');
        document.querySelector('#pricing_chart').classList.toggle('active', target_mode === 'chart');
        refreshIndexesLine(main_index);
        refreshGhmRoots(main_index, target_ghm_root);
        refreshIndexesDiff(diff_index, target_ghm_root);
        if (!downloadJson.run_lock) {
            refreshView(main_index, diff_index, target_ghm_root, Array.from(errors));
            errors.clear();
        }
    }
    this.run = run;

    function route(args)
    {
        if (args !== undefined) {
            target_mode = args.mode || target_mode;
            target_date = args.date || target_date;
            target_ghm_root = args.ghm_root || target_ghm_root;
            if (args.diff === '')
                args.diff = null;
            target_diff = (args.diff !== undefined) ? args.diff : target_diff;
        }

        if (target_mode !== null && target_date !== null && target_ghm_root !== null) {
            if (target_diff !== null) {
                switchPage('pricing/' + target_mode + '/' + target_diff + '..' + target_date + '/' + target_ghm_root);
            } else {
                switchPage('pricing/' + target_mode + '/' + target_date + '/' + target_ghm_root);
            }
        } else {
            switchPage('pricing/' + target_mode);
        }
    }
    this.route = route;

    function moveIndex(relative_index)
    {
        var index = indexes.findIndex(function(index) { return index.begin_date == target_date; });
        if (index < 0)
            index = indexes.length - 1;

        var new_index = index + relative_index;
        if (new_index < 0 || new_index >= indexes.length)
            return;

        target_date = indexes[new_index].begin_date;
        route();
    }
    this.moveIndex = moveIndex;

    // A true result actually means maybe (if we haven't download the relevant index yet)
    function checkIndexGhmRoot(index, ghm_root)
    {
        return index < 0 ||
               !indexes[index].init ||
               (pricings_map[ghm_root] && pricings_map[ghm_root][index]);
    }

    function updateIndexes(func)
    {
        if (indexes_init)
            return;

        downloadJson('api/indexes.json', {}, function(status, json) {
            var error = null;

            switch (status) {
                case 200: {
                    if (json.length > 0) {
                        indexes = json;
                        for (var i = 0; i < indexes.length; i++)
                            indexes[i].init = false;
                    } else {
                        error = 'Aucune table disponible';
                    }
                } break;

                case 404: { error = 'Liste des indexes introuvable'; } break;
                case 502:
                case 503: { error = 'Service non accessible'; } break;
                case 504: { error = 'Délai d\'attente dépassé, réessayez'; } break;
                default: { error = 'Erreur inconnue ' + status; } break;
            }

            indexes_init = !error;
            if (error)
                errors.add(error);
            if (!downloadJson.run_lock)
                func();
        });
    }

    function updateGhmRoots(func)
    {
        if (ghm_roots_init)
            return;

        downloadJson('api/ghm_roots.json', {}, function(status, json) {
            var error = null;

            switch (status) {
                case 200: {
                    if (json.length > 0) {
                        ghm_roots = json;
                        for (var i = 0; i < ghm_roots.length; i++) {
                            var ghm_root_info = ghm_roots[i];
                            ghm_roots_map[ghm_root_info.ghm_root] = ghm_root_info;
                        }
                    }

                    ghm_roots = ghm_roots.sort(function(ghm_root_info1, ghm_root_info2) {
                        if (ghm_root_info1.da !== ghm_root_info2.da) {
                            return (ghm_root_info1.da < ghm_root_info2.da) ? -1 : 1;
                        } else if (ghm_root_info1.ghm_root !== ghm_root_info2.ghm_root) {
                            return (ghm_root_info1.ghm_root < ghm_root_info2.ghm_root) ? -1 : 1;
                        } else {
                            return 0;
                        }
                    });
                } break;

                case 404: { error = 'Liste des racines de GHM introuvable'; } break;
                case 502:
                case 503: { error = 'Service non accessible'; } break;
                case 504: { error = 'Délai d\'attente dépassé, réessayez'; } break;
                default: { error = 'Erreur inconnue ' + status; } break;
            }

            ghm_roots_init = !error;
            if (error)
                errors.add(error);
            if (!downloadJson.run_lock)
                func();
        });
    }

    function updatePriceMap(index, func)
    {
        if (indexes[index].init)
            return;

        var begin_date = indexes[index].begin_date;
        downloadJson('api/price_map.json', {date: begin_date}, function(status, json) {
            var error = null;

            switch (status) {
                case 200: {
                    if (json.length > 0) {
                        for (var i = 0; i < json.length; i++) {
                            var pricing_info = pricings_map[json[i].ghm_root];
                            if (pricing_info === undefined) {
                                pricing_info = Array.apply(null, Array(indexes.length));
                                pricings_map[json[i].ghm_root] = pricing_info;
                            }
                            pricing_info[index] = json[i];
                            pricing_info[index].ghs_map = {};
                            for (var j = 0; j < pricing_info[index].ghs.length; j++) {
                                var ghs = pricing_info[index].ghs[j];
                                pricing_info[index].ghs_map[ghs.ghs] = ghs;
                            }
                        }
                    } else {
                        error = 'Aucune racine de GHM dans cette table';
                    }
                } break;

                case 404: { error = 'Aucune table disponible à cette date'; } break;
                case 502:
                case 503: { error = 'Service non accessible'; } break;
                case 504: { error = 'Délai d\'attente dépassé, réessayez'; } break;
                default: { error = 'Erreur inconnue ' + status; } break;
            }

            indexes[index].init = !error;
            if (error)
                errors.add(error);
            if (!downloadJson.run_lock)
                func();
        });
    }

    // TODO: Split refreshHeader(), remove this function
    function refreshView(main_index, diff_index, ghm_root, errors)
    {
        var ghm_root_info = ghm_roots_map[ghm_root];
        var pricing_info = pricings_map[ghm_root];
        var max_duration = parseInt(document.querySelector('#pricing_max_duration').value) + 1;

        var log = document.querySelector('#pricing .log');
        var old_table = document.querySelector('#pricing_table');
        var chart_ctx = document.querySelector('#pricing_chart').getContext('2d');

        if (errors.length) {
            log.style.display = 'block';
            log.innerHTML = errors.join('<br/>');
        } else {
            log.style.display = 'none';
        }

        if (pricing_info && pricing_info[main_index] && (diff_index < 0 || pricing_info[diff_index])) {
            if (document.querySelector('#pricing_table').classList.contains('active')) {
                var table = createTable(pricing_info, main_index, diff_index, max_duration, true);
                cloneAttributes(old_table, table);
                old_table.parentNode.replaceChild(table, old_table);
            }

            if (document.querySelector('#pricing_chart').classList.contains('active')) {
                chart = refreshChart(chart, chart_ctx, pricing_info, main_index,
                                     diff_index, max_duration);
            }
        } else {
            var table = createElement('table');
            cloneAttributes(old_table, table);
            old_table.parentNode.replaceChild(table, old_table);

            if (chart) {
                chart.destroy();
                chart = null;
            }
        }
        markOutdated('#pricing_view', false);
    }

    function refreshIndexesLine(main_index)
    {
        var g = createElementNS('svg', 'g', {});

        if (indexes.length >= 2) {
            var first_date = new Date(indexes[0].begin_date);
            var last_date = new Date(indexes[indexes.length - 1].begin_date);
            var max_delta = last_date - first_date;

            var text_above = true;
            for (var i = 0; i < indexes.length; i++) {
                var date = new Date(indexes[i].begin_date);

                var x = (9.0 + (date - first_date) / max_delta * 82.0).toFixed(1) + '%';
                var radius = indexes[i].changed_prices ? 5 : 4;
                if (i == main_index) {
                    radius++;
                    var color = '#ff8900';
                    var weight = 'bold';
                } else if (indexes[i].changed_prices) {
                    var color = '#000';
                    var weight = 'normal';
                } else {
                    var color = '#888';
                    var weight = 'normal';
                }
                var click_function = (function() {
                    var index = i;
                    return function(e) { route({date: indexes[index].begin_date}); };
                })();

                var node = createElementNS('svg', 'circle',
                                           {cx: x, cy: 20, r: radius, fill: color,
                                            style: 'cursor: pointer;',
                                            click: click_function},
                    createElementNS('svg', 'title', {}, indexes[i].begin_date)
                );
                g.appendChild(node);

                if (indexes[i].changed_prices) {
                    var text_y = text_above ? 10 : 40;
                    text_above = !text_above;

                    var text = createElementNS('svg', 'text',
                                               {x: x, y: text_y, 'text-anchor': 'middle', fill: color,
                                                style: 'cursor: pointer; font-weight: ' + weight,
                                                click: click_function},
                                               indexes[i].begin_date);
                    g.appendChild(text);
                }
            }
        }

        var old_g = document.querySelector('#pricing_indexes > g');
        old_g.parentNode.replaceChild(g, old_g);
    }

    function refreshIndexesDiff(diff_index, test_ghm_root)
    {
        var el = document.querySelector("#pricing_diff_indexes");
        el.innerHTML = '';

        el.appendChild(createElement('option', {value: ''}, 'Désactiver'));
        for (var i = 0; i < indexes.length; i++) {
            var opt = createElement('option', {value: indexes[i].begin_date},
                                    indexes[i].begin_date);
            if (i === diff_index)
                opt.setAttribute('selected', '');
            if (!checkIndexGhmRoot(i, test_ghm_root)) {
                opt.setAttribute('disabled', '');
                opt.text += '*';
            }
            el.appendChild(opt);
        }
    }

    function refreshGhmRoots(index, select_ghm_root)
    {
        var el = document.querySelector('#pricing_ghm_roots');
        el.innerHTML = '';

        for (var i = 0; i < ghm_roots.length;) {
            var da = ghm_roots[i].da;

            var optgroup = createElement('optgroup', {label: da + ' – ' + ghm_roots[i].da_desc});
            for (; i < ghm_roots.length && ghm_roots[i].da === da; i++) {
                var ghm_root_info = ghm_roots[i];

                var opt = createElement('option', {value: ghm_root_info.ghm_root},
                                        ghm_root_info.ghm_root + ' – ' + ghm_root_info.desc);
                if (!checkIndexGhmRoot(index, ghm_root_info.ghm_root)) {
                    opt.setAttribute('disabled', '');
                    opt.text += '*';
                }
                optgroup.appendChild(opt);
            }
            el.appendChild(optgroup);
        }
        if (select_ghm_root)
            el.value = select_ghm_root;
    }

    function refreshChart(chart, chart_ctx, pricing_info, main_index, diff_index, max_duration)
    {
        var ghs = pricing_info[main_index].ghs;

        function ghsLabel(ghs)
        {
            return '' + ghs.ghs + (ghs.conditions.length ? '*' : '') + ' (' + ghs.ghm + ')';
        }

        function modeToColor(mode)
        {
            return {
                "J": "#fdae6b",
                "T": "#fdae6b",
                "1": "#fd8d3c",
                "2": "#f16913",
                "3": "#d94801",
                "4": "#a63603",
                "A": "#fd8d3c",
                "B": "#f16913",
                "C": "#d94801",
                "D": "#a63603",
                "E": "#7f2704",
                "Z": "#9a9a9a"
            }[mode] || "black";
        }

        var data = {
            labels: [],
            datasets: []
        };
        for (var i = 0; i < max_duration; i++)
            data.labels.push(durationText(i));

        var max_price = 0.0;
        for (var i = 0; i < ghs.length; i++) {
            var dataset = {
                label: ghsLabel(ghs[i]),
                data: [],
                borderColor: modeToColor(ghs[i].ghm_mode),
                backgroundColor: modeToColor(ghs[i].ghm_mode),
                borderDash: (ghs[i].conditions.length ? [5, 5] : undefined),
                fill: false
            };
            for (var duration = 0; duration < max_duration; duration++) {
                if (diff_index < 0) {
                    var info = computePrice(ghs[i], duration);
                } else {
                    var info = computePriceDelta(ghs[i], pricing_info[diff_index].ghs_map[ghs[i].ghs], duration);
                }

                if (info !== null) {
                    dataset.data.push({
                        x: durationText(duration),
                        y: info[0] / 100
                    });
                    max_price = Math.max(max_price, Math.abs(info[0]));
                } else {
                    dataset.data.push(null);
                }
            }
            data.datasets.push(dataset);
        }

        var min_price;
        if (diff_index < 0) {
            min_price = 0.0;

            // Recalculate maximum price across all (loaded) indexes to stabilize Y axis
            for (var i = 0; i < pricing_info.length; i++) {
                if (i === main_index || !pricing_info[i])
                    continue;

                for (var j = 0; j < pricing_info[i].ghs.length; j++) {
                    p = computePrice(pricing_info[i].ghs[j], max_duration - 1);
                    if (p && p[0] > max_price)
                        max_price = p[0];
                }
            }
            max_price /= 100.0;
        } else {
            max_price /= 100.0;
            min_price = -max_price;
        }

        if (chart) {
            chart.data = data;
            chart.options.scales.yAxes[0].ticks.suggestedMin = min_price;
            chart.options.scales.yAxes[0].ticks.suggestedMax = max_price;
            chart.update({duration: 0});
        } else {
            chart = new Chart(chart_ctx, {
                type: 'line',
                data: data,
                options: {
                    responsive: true,
                    tooltips: {mode: 'index', intersect: false},
                    hover: {mode: 'x', intersect: true},
                    elements: {
                        line: {tension: 0},
                        point: {radius: 0, hitRadius: 0}
                    },
                    scales: {
                        yAxes: [
                            {ticks: {suggestedMin: min_price, suggestedMax: max_price}}
                        ]
                    }
                },
            });
        }

        return chart;
    }

    function createTable(pricing_info, main_index, diff_index, max_duration, merge_cells)
    {
        var ghs = pricing_info[main_index].ghs;

        if (max_duration === undefined)
            max_duration = 200;
        if (merge_cells === undefined)
            merge_cells = true;

        function appendRow(parent, name, func)
        {
            var tr =
                createElement('tr', {},
                    createElement('th', {}, name)
                );

            var prev_cell = [document.createTextNode(''), {}, false];
            var prev_td = null;
            for (var i = 0; i < ghs.length; i++) {
                var cell = func(ghs[i], i) || [null, {}, false];
                if (cell[0] === null) {
                    cell[0] = document.createTextNode('');
                } else if (typeof cell[0] === 'string') {
                    cell[0] = document.createTextNode(cell[0]);
                }
                if (merge_cells && cell[2] && cell[0].isEqualNode(prev_cell[0]) &&
                        cell[1].class === prev_cell[1].class) {
                    var colspan = parseInt(prev_td.getAttribute('colspan') || 1);
                    prev_td.setAttribute('colspan', colspan + 1);
                } else {
                    prev_td = tr.appendChild(createElement('td', cell[1], cell[0]));
                }
                prev_cell = cell;
            }

            parent.appendChild(tr);
        }

        var table =
            createElement('table', {},
                createElement('thead'),
                createElement('tbody')
            );
        var thead = table.querySelector('thead');
        var tbody = table.querySelector('tbody');

        appendRow(thead, 'GHS', function(col) { return ['' + col.ghs, {class: 'desc'}, true]; });
        appendRow(thead, 'GHM', function(col) { return [col.ghm, {class: 'desc'}, true]; });
        appendRow(thead, 'Niveau', function(col) { return ['Niveau ' + col.ghm_mode, {class: 'desc'}, true]; });
        appendRow(thead, 'Conditions', function(col) {
            var el =
                createElement('div', {title: col.conditions.join('\n')},
                    col.conditions.length ? col.conditions.length.toString() : ''
                );
            return [el, {class: 'conditions'}, true];
        });
        appendRow(thead, 'Borne basse', function(col) { return [durationText(col.exb_treshold), {class: 'exb'}, true]; });
        appendRow(thead, 'Borne haute',
                  function(col) { return [durationText(col.exh_treshold && col.exh_treshold - 1), {class: 'exh'}, true]; });
        appendRow(thead, 'Tarif €', function(col) { return [priceText(col.ghs_cents), {class: 'price'}, true]; });
        appendRow(thead, 'Forfait EXB €',
                  function(col) { return [col.exb_once ? priceText(col.exb_cents) : '', {class: 'exb'}, true]; });
        appendRow(thead, 'Tarif EXB €',
                  function(col) { return [!col.exb_once ? priceText(col.exb_cents) : '', {class: 'exb'}, true]; });
        appendRow(thead, 'Tarif EXH €', function(col) { return [priceText(col.exh_cents), {class: 'exh'}, true]; });
        appendRow(thead, 'Age', function(col) {
            var texts = [];
            if (col.ghm_mode >= '1' && col.ghm_mode < '5') {
                var severity = col.ghm_mode.charCodeAt(0) - '1'.charCodeAt(0);
                if (severity < col.young_severity_limit)
                    texts.push('< ' + col.young_age_treshold.toString());
                if (severity < col.old_severity_limit)
                    texts.push('≥ ' + col.old_age_treshold.toString());
            }
            return [texts.join(', '), {class: 'age'}, true];
        });

        for (var duration = 0; duration < max_duration; duration++) {
            appendRow(tbody, durationText(duration), function(col, i) {
                if (diff_index < 0) {
                    var info = computePrice(col, duration);
                } else {
                    var info = computePriceDelta(col, pricing_info[diff_index].ghs_map[col.ghs], duration);
                }
                if (info === null)
                    return null;

                var props = [priceText(info[0]), {class: info[1]}, false];
                if (duration == 0 && col.warn_cmd28) {
                    props[1].class += ' warn';
                    props[1].title = 'Devrait être orienté dans la CMD 28 (séance)';
                }
                return props;
            });
        }

        return table;
    }

    function computePrice(ghs, duration)
    {
        var duration_mask;
        if (duration < 32) {
            duration_mask = 1 << duration;
        } else {
            duration_mask = 1 << 31;
        }
        if (!(ghs.durations & duration_mask))
            return null;

        if (ghs.exb_treshold && duration < ghs.exb_treshold) {
            var price_cents = ghs.ghs_cents;
            if (ghs.exb_once) {
                price_cents -= ghs.exb_cents;
            } else {
                price_cents -= (ghs.exb_treshold - duration) * ghs.exb_cents;
            }
            return [price_cents, 'exb'];
        } else if (ghs.exh_treshold && duration >= ghs.exh_treshold) {
            var price_cents = ghs.ghs_cents + (duration - ghs.exh_treshold + 1) * ghs.exh_cents;
            return [price_cents, 'exh'];
        } else {
            return [ghs.ghs_cents, 'price'];
        }
    }

    function computePriceDelta(ghs, prev_ghs, duration)
    {
        var p1 = ghs ? computePrice(ghs, duration) : null;
        var p2 = prev_ghs ? computePrice(prev_ghs, duration) : null;

        if (p1 !== null && p2 !== null) {
            var delta = p1[0] - p2[0];
            if (delta < 0) {
                return [delta, 'lower'];
            } else if (delta > 0) {
                return [delta, 'higher'];
            } else {
                return [0, 'neutral'];
            }
        } else if (p1 !== null) {
            return [p1[0], 'added'];
        } else if (p2 !== null) {
            return [-p2[0], 'removed'];
        } else {
            return null;
        }
    }

    function durationText(duration)
    {
        if (duration !== undefined) {
            return duration.toString() + (duration >= 2 ? ' nuits' : ' nuit');
        } else {
            return '';
        }
    }
    this.durationText = durationText;

    function priceText(price_cents)
    {
        if (price_cents !== undefined) {
            // The replace() is a bit dirty, but it gets the job done
            return (price_cents / 100.0).toFixed(2).replace('.', ',');
        } else {
            return '';
        }
    }
    this.priceText = priceText;
}).call(pricing);
