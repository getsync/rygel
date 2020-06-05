#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import xml.etree.ElementTree as ET
import json
import sys

sys.stdout.reconfigure(encoding = 'utf-8')

doc = ET.parse('cim10.xml')
root = doc.getroot()

modifiers_map = {}
definitions = {}

for mod in root.findall('.//Modifier'):
    code = mod.attrib['code']
    classes = root.findall('.//ModifierClass[@modifier="' + code + '"]')

    labels = {}
    modifiers_map[code] = labels

    for cl in classes:
        code = cl.attrib['code'].replace('.', '')
        label = cl.find('./Rubric[@kind="preferred"]/Label').text

        if not code.endswith(' '):
            labels[code] = label

def generate_modifiers(code, label, modifiers):
    if not modifiers:
        return

    modifier = modifiers[0]
    for k, v in modifier.items():
        code2 = code + k
        label2 = f'{label}, {v}'
        definitions[code2] = label2

        generate_modifiers(code2, label2, modifiers[1:])

for cl in root.findall('.//Class[@kind="category"]'):
    code = cl.attrib['code'].replace('.', '')
    label = cl.find('./Rubric[@kind="preferred"]/Label').text
    definitions[code] = label

    modifiers = {}
    excluded = []

    it = cl
    ancestry = 0
    while it is not None:
        for modified_by in it.findall('./ModifiedBy'):
            modified_by = modified_by.attrib['code']
            if not modified_by in modifiers:
                modifiers[modified_by] = modifiers_map[modified_by]

        if it != cl:
            for exclude in it.findall('./ExcludeModifier'):
                exclude = exclude.attrib['code']
                excluded.append(exclude)

        it = it.find('./SuperClass')
        if it is not None:
            it = root.find('./Class[@code="' + it.attrib['code'] + '"]')

        ancestry += 1

    # if excluded:
    #     print(code, excluded)

    # print(code, list(modifiers.keys()), excluded)

    modifiers = [v for k, v in modifiers.items() if not k in excluded]
    generate_modifiers(code, label, modifiers)

definitions = [{'code': k, 'label': v} for k, v in definitions.items()]
definitions.sort(key = lambda defn: defn['code'])

cim10 = {
    'diagnoses': {
        'title': 'CIM-10',
        'definitions': definitions
    }
}

print(json.dumps(cim10, indent = 4, ensure_ascii = False))
