page.output(html`
    <p><b>Attention</b>, il s'agit des données locales de la machine. Pour exporter
    les données du serveur, vous devez d'abord effectuer une synchronisation.</p>

    <button class="md_button">Synchroniser les données</button>
    <button class="md_button" @click=${exportData}>Exporter les données</button>
`)

async function exportData() {
    if (typeof XSLX === 'undefined')
        await net.loadScript(`${env.base_url}static/xlsx.core.min.js`)

    let wb = XLSX.utils.book_new();

    for (let form of app.forms) {
        if (form.key.startsWith('F_') || form.key === 'Patient') {
            let [records, columns] = await Promise.all([
                vrec.loadAll(form.key),
                vrec.listColumns(form.key)
            ]);

            columns = form_exec.orderColumns(form.pages, columns);

            let ws = XLSX.utils.aoa_to_sheet([
                columns.map(col => col.title),
                ...records.map(record => columns.map(col => {
                    let value = record.values[col.variable];

                    if (Array.isArray(value)) {
                        if (col.hasOwnProperty('prop')) {
                            return value.includes(col.prop) ? 1 : 0;
                        } else {
                            return value.join('|');
                        }
                    } else {
                        return value;
                    }
                }))
            ]);

            XLSX.utils.book_append_sheet(wb, ws, form.key);
        }
    }

    let date = new Date;
    XLSX.writeFile(wb, `Medita${date.toISOString()}.xlsx`, {cellDates: true});
}
