var fso = new ActiveXObject("Scripting.FileSystemObject");
var scriptPath = fso.GetParentFolderName(WScript.ScriptFullName);

// Пути для первой задачи (списки макросов)
var VERTEX_MACRO_FILE = fso.BuildPath(scriptPath, "shaders/vsList.h");
var PIXEL_MACRO_FILE = fso.BuildPath(scriptPath, ".haders/psList.h");

// Путь для новой задачи (общий файл структур cbuffer)
var CBUFFER_LIST_FILE = fso.BuildPath(scriptPath, "shaders/shadersList.h");

function main() {
    //WScript.Echo("--- Старт генерации ресурсов шейдеров ---");

    // Абсолютные пути к исходным папкам
    var vertexSrc = fso.BuildPath(scriptPath, "shaders/vs");
    var pixelSrc = fso.BuildPath(scriptPath, "shaders/ps");

    generateMacroList(vertexSrc, VERTEX_MACRO_FILE);
    generateMacroList(pixelSrc, PIXEL_MACRO_FILE);

    var cbBuffer = fso.CreateTextFile(CBUFFER_LIST_FILE, true);
    cbBuffer.Close();

    var cbOutFile = fso.OpenTextFile(CBUFFER_LIST_FILE, 8, true);

    // Сканируем папки и дописываем структуры констбуферов
    parseShaderConstants(cbOutFile, vertexSrc, "vs");
    parseShaderConstants(cbOutFile, pixelSrc, "ps");

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

// Новая функция для парсинга cbuffer params : register(b0)
function parseShaderConstants(fileStream, srcPath, outerNamespace) {
    if (!fso.FolderExists(srcPath)) return;

    var folder = fso.GetFolder(srcPath);
    var fc = new Enumerator(folder.Files);

    // Регулярное выражение: ищет cbuffer params : register(b0), игнорируя любые пробелы/табы
    var cbufferRegex = /cbuffer\s+params\s*:\s*register\s*\(\s*b0\s*\)\s*\{([\s\S]*?)\}/i;

    for (; !fc.atEnd(); fc.moveNext()) {
        var file = fc.item();

        // Открываем файл шейдера для чтения (1 = ForReading, -2 = Системная кодировка)
        var stream = file.OpenAsTextStream(1, -2);
        var content = stream.ReadAll();
        stream.Close();

        var match = cbufferRegex.exec(content);

        // Если нашли нужный cbuffer
        if (match !== null) {
            var innerContent = match[1]; // Получаем всё, что строго между скобок { }
            var shaderName = fso.GetBaseName(file.Name);

            // Записываем структуру в общий файл
            fileStream.WriteLine("namespace " + outerNamespace + " {");
            fileStream.WriteLine("");
            fileStream.WriteLine("    namespace " + shaderName + " {");
            fileStream.WriteLine("");
            fileStream.WriteLine("        struct");
            fileStream.WriteLine("        {");

            fileStream.Write(innerContent);

            if (innerContent.charAt(innerContent.length - 1) !== "\n") {
                fileStream.WriteLine("");
            }

            fileStream.WriteLine("        } params;");
            fileStream.WriteLine("");
            fileStream.WriteLine("    }");
            fileStream.WriteLine("");
            fileStream.WriteLine("}");
            fileStream.WriteLine("");
        }
    }
}

main();
