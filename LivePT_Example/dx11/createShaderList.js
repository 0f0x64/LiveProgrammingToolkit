var fso = new ActiveXObject("Scripting.FileSystemObject");
var scriptPath = fso.GetParentFolderName(WScript.ScriptFullName);

// Пути для первой задачи (списки макросов)
var VERTEX_MACRO_FILE = fso.BuildPath(scriptPath, "../shaders/vsList.h");
var PIXEL_MACRO_FILE = fso.BuildPath(scriptPath, "../shaders/psList.h");

// Путь для новой задачи (общий файл структур cbuffer)
var CBUFFER_LIST_FILE = fso.BuildPath(scriptPath, "../shaders/shadersList.h");

function main() {
    // Абсолютные пути к исходным папкам
    var vertexSrc = fso.BuildPath(scriptPath, "../shaders/vs");
    var pixelSrc = fso.BuildPath(scriptPath, "../shaders/ps");

    generateMacroList(vertexSrc, VERTEX_MACRO_FILE);
    generateMacroList(pixelSrc, PIXEL_MACRO_FILE);

    var cbBuffer = fso.CreateTextFile(CBUFFER_LIST_FILE, true);
    cbBuffer.Close();

    var cbOutFile = fso.OpenTextFile(CBUFFER_LIST_FILE, 8, true);

    // Обработка вершинных шейдеров с однократным открытием неймспейса
    if (fso.FolderExists(vertexSrc)) {
        cbOutFile.WriteLine("namespace vs {");
        cbOutFile.WriteLine("");
        parseShaderConstants(cbOutFile, vertexSrc);
        cbOutFile.WriteLine("}");
        cbOutFile.WriteLine("");
    }

    // Обработка пиксельных шейдеров с однократным открытием неймспейса
    if (fso.FolderExists(pixelSrc)) {
        cbOutFile.WriteLine("namespace ps {");
        cbOutFile.WriteLine("");
        parseShaderConstants(cbOutFile, pixelSrc);
        cbOutFile.WriteLine("}");
        cbOutFile.WriteLine("");
    }

    cbOutFile.Close();

    WScript.Echo("[graphics Api prebuld] shader lists created successfully");
}

// Старая проверенная функция для макросов shader(имя)
function generateMacroList(srcPath, targetFilePath) {
    if (!fso.FolderExists(srcPath)) return;

    var outFile = fso.CreateTextFile(targetFilePath, true);
    var folder = fso.GetFolder(srcPath);
    var fc = new Enumerator(folder.Files);

    for (; !fc.atEnd(); fc.moveNext()) {
        var file = fc.item();
        var baseName = fso.GetBaseName(file.Name);
        outFile.WriteLine("shader(" + baseName + ")");
    }
    outFile.Close();
}

// Функция парсинга с добавлением правильных отступов для содержимого cbuffer
function parseShaderConstants(fileStream, srcPath) {
    var folder = fso.GetFolder(srcPath);
    var fc = new Enumerator(folder.Files);

    // Регулярное выражение: ищет cbuffer params : register(b0)
    var cbufferRegex = /cbuffer\s+params\s*:\s*register\s*\(\s*b0\s*\)\s*\{([\s\S]*?)\}/i;

    for (; !fc.atEnd(); fc.moveNext()) {
        var file = fc.item();

        var stream = file.OpenAsTextStream(1, -2);
        var content = stream.ReadAll();
        stream.Close();

        var match = cbufferRegex.exec(content);
        var shaderName = fso.GetBaseName(file.Name);

        fileStream.WriteLine("    namespace " + shaderName + " {");
        fileStream.WriteLine("");
        fileStream.WriteLine("        struct");
        fileStream.WriteLine("        {");

        if (match !== null) {
            var innerContent = match[1];

            // Разделяем контент на отдельные строки
            var lines = innerContent.split(/\r?\n/);

            for (var i = 0; i < lines.length; i++) {
                var line = lines[i];

                // Пропускаем пустые строки в самом начале или конце, чтобы не плодить лишние пробелы
                if (i === 0 && line.replace(/^\s+|\s+$/g, '') === '') continue;
                if (i === lines.length - 1 && line.replace(/^\s+|\s+$/g, '') === '') continue;

                // Убираем у исходной строки начальные пробелы (если они были в HLSL файле) 
                // и добавляем фиксированные 12 пробелов (3 таба: 1 для namespace, 1 для struct, 1 для полей)
                var trimmedLine = line.replace(/^\s+/, '');

                // Если строка не пустая после трима, пишем с отступом, иначе просто пустую строку
                if (trimmedLine !== '') {
                    fileStream.WriteLine("            " + trimmedLine);
                } else {
                    fileStream.WriteLine("");
                }
            }
        }

        fileStream.WriteLine("        } params;");
        fileStream.WriteLine("");
        fileStream.WriteLine("    }");
        fileStream.WriteLine("");
    }
}

main();
