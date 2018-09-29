// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

let mco_common = {};
(function() {
    'use strict';

    const ConceptSets = {
        'ccam': {path: 'concepts/ccam.json', key: 'procedure'},
        'cim10': {path: 'concepts/cim10.json', key: 'diagnosis'},
        'mco_ghm_roots': {path: 'concepts/mco_ghm_roots.json', key: 'ghm_root'}
    };

    // Cache
    let indexes = [];
    let concept_sets = {};

    function updateIndexes()
    {
        if (!indexes.length) {
            let url = thop.baseUrl('api/mco_indexes.json');
            data.get(url, function(json) {
                indexes = json;
            });
        }

        return indexes;
    }
    this.updateIndexes = updateIndexes;

    function updateConceptSet(name)
    {
        let info = ConceptSets[name];
        let set = concept_sets[name];

        if (info && !set) {
            set = {
                concepts: [],
                map: {}
            };
            concept_sets[name] = set;

            let url = thop.baseUrl(info.path);
            data.get(url, function(json) {
                set.concepts = json;
                for (const concept of json)
                    set.map[concept[info.key]] = concept;
            });
        }

        return set;
    }
    this.updateConceptSet = updateConceptSet;

    function refreshIndexes(indexes, main_index)
    {
        let builder = new VersionLine;

        for (const index of indexes) {
            let label = index.begin_date;
            if (label.endsWith('-01'))
                label = label.substr(0, label.length - 3);

            builder.addVersion(index.begin_date, label, index.begin_date, index.changed_prices);
        }
        if (main_index >= 0)
            builder.setValue(indexes[main_index].begin_date);

        builder.anchorBuilder = function(version) {
            return thop.routeToUrl({date: version.date});
        };
        builder.changeHandler = function() {
            thop.go({date: this.object.getValue()});
        };

        let svg = builder.getWidget();
        let old_svg = query('#opt_indexes');
        svg.copyAttributesFrom(old_svg);
        old_svg.replaceWith(svg);
    }
    this.refreshIndexes = refreshIndexes;

    function durationText(duration)
    {
        if (duration !== undefined) {
            return duration.toString() + (duration >= 2 ? ' nuits' : ' nuit');
        } else {
            return '';
        }
    }
    this.durationText = durationText;
}).call(mco_common);
