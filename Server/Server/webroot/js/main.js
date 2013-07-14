var filters    = angular.module('tricorder.filters', [])
var directives = angular.module('tricorder.directives', [])
var app        = angular.module('tricorder.app', ['tricorder.filters', 'tricorder.directives']);

filters.filter('join', function () {
        return function(list, joiner) {
                    if (joiner == undefined) joiner = ", "
            if (list   == undefined) list   = []
            return list.join(joiner)
        }
});

app.run(["$rootScope", function($rootScope) {
    console.log("helu");
});
