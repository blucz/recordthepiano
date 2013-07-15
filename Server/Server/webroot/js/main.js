var filters    = angular.module('recordthepiano.filters', [])
var directives = angular.module('recordthepiano.directives', [])
var app        = angular.module('recordthepiano.app', ['recordthepiano.filters', 'recordthepiano.directives']);

filters.filter('join', function () {
        return function(list, joiner) {
                    if (joiner == undefined) joiner = ", "
            if (list   == undefined) list   = []
            return list.join(joiner)
        }
});

app.run(["$rootScope", function($rootScope) {
    console.log("helu");
}]);

function MainController($rootScope, $scope) {
    $.get("/api/list").done(function(data) {
        console.log(data)
        $scope.$apply(function() {
            $scope.recordings = data.recordings;
        });
    });
}
