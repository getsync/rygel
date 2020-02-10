// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Stub lithtml functions
var render = () => {};
var html = () => {};
var svg = () => {};

var server = new function() {
    let self = this;

    this.validateRecord = function(code, values) {
        let page = new Page;

        // We don't care about PageState (single execution)
        let page_builder = new PageBuilder(new PageState, page);
        page_builder.getValue = (key, default_value) => getValue(values, key, default_value);

        // Execute user script
        // XXX: We should fail when data types don't match (string given to number widget)
        let func = Function('data', 'route', 'go', 'form', 'page', 'scratch', code);
        func({}, {}, () => {}, page_builder, page_builder, {});

        let values2 = filterValues(values, page.variables);
        let variables = page.variables.map(variable => variable.key);

        // Make it easy for the C++ caller to store in database
        let ret = {
            json: JSON.stringify(values2),
            variables: variables,
            errors: page.errors.length
        };

        return ret;
    };

    function getValue(values, key, default_value) {
        if (!values.hasOwnProperty(key)) {
            values[key] = default_value;
            return default_value;
        }

        return values[key];
    }

    function filterValues(values, variables) {
        let values2 = {};

        let set = new Set;
        for (let variable of variables) {
            set.add(variable.key);
            values2[variable.key] = values[variable.key];
        }

        for (let key in values) {
            if (!set.has(key))
                throw new Error(`Unexpected variable '${key}'`);
        }

        return values2;
    }
};