<!Doctype html>
<html>
    <head>
        <link rel="stylesheet" href="searchPage.css">
    </head>
    <body>
        <div id="searchDiv">
            <form id="searchInput" action="/search" method="GET">
                <input name="q" type="text">
            </form>
        </div>
        <div id="searchItemContainer">
            {{ITEMS}}
        </div>

        <script src="index.js"></script>
    </body>
</html>
