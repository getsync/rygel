shared.makeHeader("Suivi d'échelles", page)
route.ctx = page.text("id", "Identifiant", {value: route.ctx, compact: true, readonly: true}).value

let docx = page.file('docx', 'Modèle de document', {compact: true})

page.output(html`
    <br/>
    <button class="md_button" ?disabled=${docx.missing}
            @click=${transformDocX}>Générer le document</button>
`)

async function transformDocX() {
    if (typeof Docxtemplater === 'undefined')
        await net.loadScript(`${env.base_url}static/docxtemplater.pk.js`)

    let data = {
        patient: values
    };

    let reader = new FileReader;
    reader.onload = function() {
        let zip = new PizZip(reader.result);
        let doc = new Docxtemplater().loadZip(zip);
    
        try {
            doc.setOptions({
                paragraphLoop: true,
                linebreaks: true
            })
            doc.setData(data);
            doc.render();
    
            let out = doc.getZip().generate({
                type: 'blob',
                mimeType: 'application/vnd.openxmlformats-officedocument.wordprocessingml.document'
            });
    
            let date = new Date;
            util.saveBlob(out, `CR_${values.id}_${date.toISOString()}.docx`);
        } catch (e) {
            let error_count = 0;
            if (e.properties && e.properties.id === 'multi_error') {
                e.properties.errors.forEach(function(err) {
                    console.log(err);
                    error_count++;
                });
            } else {
                console.log(e);
                error_count = 1;
            }

            alert(`Got ${error_count} error(s), open the JS console to get more information`);
        }
    }
    reader.readAsBinaryString(docx.value);
}
