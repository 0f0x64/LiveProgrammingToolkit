var fso = new ActiveXObject("Scripting.FileSystemObject");

// Главная функция, которая дергает рабочую функцию для разных папок и файлов
function main() {

    // Первый вызов: обрабатываем вершинные шейдеры
    processFolder("./shaders/vs", "./shaders/vsList.h");

    // Второй вызов: обрабатываем пиксельные шейдеры в другой файл
    processFolder("./shaders/ps", "./shaders/psList.h");

    // Сюда можно добавить еще вызовы при необходимости
}

// Рабочая функция, которая принимает два пути
function processFolder(srcPath, targetFilePath) {
    // Если исходная папка не существует, ничего не делаем
    if (!fso.FolderExists(srcPath)) {
        return;
    }

    // Создаем новый файл (если старый существовал — он полностью сотрется)
    var outFile = fso.CreateTextFile(targetFilePath, true);

    var folder = fso.GetFolder(srcPath);
    var fc = new Enumerator(folder.Files);

    // Читаем все файлы подряд без фильтрации по расширениям
    for (; !fc.atEnd(); fc.moveNext()) {
        var file = fc.item();

        // Получаем имя без пути и расширения
        var baseName = fso.GetBaseName(file.Name);

        // Записываем хардкод-строку shader(имя)
        outFile.WriteLine("shader(" + baseName + ")");
    }

    // Закрываем готовый файл
    outFile.Close();
}

// Запуск программы
main();
