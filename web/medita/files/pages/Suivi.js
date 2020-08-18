shared.makeHeader("Suivi d'échelles", page)
route.ctx = page.text("id", "Identifiant", {value: route.ctx, compact: true, readonly: true}).value

page.pushOptions({compact: true})

// Graphique et tableaux
if (values.id) {
    let select = new Set(shared.echelles.map(echelle => echelle.form));
    let echelles = shared.echelles.filter(echelle => select.delete(echelle.form));
    
    let p = Promise.all([
        window.Chart || net.loadScript(`${env.base_url}files/resources/chart.bundle.min.js`),
        ...echelles.map(echelle => vrec.loadAll(echelle.form))
    ]);

    scratch.canvas = scratch.canvas || document.createElement('canvas');
    scratch.div = scratch.div || document.createElement('div');
    page.output(scratch.canvas);
    page.output(scratch.div);

    p.then(tables => {
        // Remove parasite script thingy
        tables.shift();
        tables = tables.map(table => table.filter(record => record.values.id == values.id));

        if (scratch.echelles == null) {
            scratch.echelles = new Set;

            for (let i = 0; i < echelles.length; i++) {
                let echelle = echelles[i];
                let records = tables[i];

                if (records.length >= 2)
                    scratch.echelles.add(echelle.form);
            }
        }

        updateChart(echelles, tables, scratch.echelles);
        updateTables(echelles, tables);
    });
}

function updateChart(echelles, tables, show_set) {
    let canvas = scratch.canvas;
    let chart = scratch.chart;

    let colors = ['#236b20', '#24579d', 'rgb(187, 35, 35)', 'black'];

    if (!chart) {
        chart = new Chart(canvas.getContext('2d'), {
            type: 'line',
            data: {
                labels: [],
                datasets: []
            },
            options: {
                legend: {
                    position: 'bottom',
                    align: 'start'
                },
                scales: {
                    xAxes: [{
                        type: 'time',

                        time: {
                            unit: 'day',
                            unitStepSize: 1,
                            displayFormats: {
                               day: 'DD/MM HH:mm'
                            }
                        },

                        ticks: {
                            source: 'data',
                            minRotation: 90,
                            maxRotation: 90
                        }
                    }],
                    yAxes: [{type: 'linear'}]
                },
                onClick: function(x, e) { console.log(x, e) }
            }
        });
        scratch.chart = chart;
    }
    chart.data.datasets.length = 0;

    for (let i = 0; i < echelles.length; i++) {
        let echelle = echelles[i];
        let records = tables[i];
        
        if (!show_set.has(echelle.form))
            continue;

        if (records.length) {
            let dataset = {
                label: echelle.name,
                borderColor: colors[i % colors.length],
                fill: false,
                lineTension: 0.1,
                borderDash: [],
                pointBorderColor: [],
                pointBackgroundColor:[],
                data: []
            };
            chart.data.datasets.push(dataset);

            for (let record of records) {
                dataset.pointBorderColor.push(colors[i % colors.length]);
                dataset.pointBackgroundColor.push(colors[i % colors.length]);

                dataset.data.push({
                    x: new Date(util.decodeULIDTime(record.id)),
                    y: record.values.score
                });
            }
        }
    }

    canvas.style.display = chart.data.datasets.length ? 'block' : 'none';
    chart.update({duration: 0});
}

function updateTables(echelles, tables) {
    let div = scratch.div;

    render(echelles.map((echelle, idx) => {
        let records = tables[idx];

        if (records.length) {
            return html`
                <fieldset class="af_container af_section">
                    <legend>${echelle.name}</legend>
                            
                    <table class="md_table">
                        <thead>
                            <tr>
                                <th>Date</th>
                                <th>Score</th>
                                <th style="width: 200px;"></th>
                            </tr>
                        </thead>
                        <tbody>${records.map(record => {
                            let date = new Date(util.decodeULIDTime(record.id));

                            return html`<tr>
                                <td>${date.toLocaleString()}</td>
                                <td>${record.values.score}</td>
                                <td style="text-align: right;">
                                    <a href=${nav.link(`${echelle.form}/${echelle.form}`, {id: record.id})}>Modifier</a>&nbsp;&nbsp;&nbsp;
                                    <a @click=${e => showDeleteDialog(e, echelle.form, record.id)}>Supprimer</a>
                                </td>
                            </tr>`;
                        })}</tbody>
                    </table>

                    <div style="margin-top: 1em; width: 100%;" class="af_multi">
                        <input id=${'chart' + echelle.form} type="checkbox" .checked=${scratch.echelles.has(echelle.form)}
                               @click=${e => toggleChartScore(echelle.form)} />
                        <label for=${'chart' + echelle.form}>Afficher sur le graphique</label>

                        <a style="display: block; float: right;"
                           href=${nav.link(`${echelle.form}/${echelle.form}`)}>Ajouter une mesure</a>
                    </div>
                </fieldset>
            `;
        } else {
            return '';
        }
    }), div);
}

function toggleChartScore(form) {
    if (!scratch.echelles.delete(form))
        scratch.echelles.add(form);

    page.changeHandler(page);
}

function showDeleteDialog(e, form, id) {
    goupile.popup(e, 'Supprimer', (popup, close) => {
        popup.output('Voulez-vous vraiment supprimer cet enregistrement ?');

        popup.submitHandler = async () => {
            close();

            await vrec.delete(form, id);
            page.restart();
        };
    });
}

form.output(html`
    <br/>
    <button class="md_button" @click=${e => go("Accueil")}>Retour à l'accueil</button>
`)
