// Приведенный ниже блок ifdef — это стандартный метод создания макросов, упрощающий процедуру
// экспорта из библиотек DLL. Все файлы данной DLL скомпилированы с использованием символа CHEATCORE_EXPORTS
// Символ, определенный в командной строке. Этот символ не должен быть определен в каком-либо проекте,
// использующем данную DLL. Благодаря этому любой другой проект, исходные файлы которого включают данный файл, видит
// функции CHEATCORE_API как импортированные из DLL, тогда как данная DLL видит символы,
// определяемые данным макросом, как экспортированные.
#ifdef CHEATCORE_EXPORTS
#define CHEATCORE_API __declspec(dllexport)
#else
#define CHEATCORE_API __declspec(dllimport)
#endif

// Этот класс экспортирован из библиотеки DLL
class CHEATCORE_API CCheatCore {
public:
	CCheatCore(void);
	// TODO: добавьте сюда свои методы.
};

extern CHEATCORE_API int nCheatCore;

CHEATCORE_API int fnCheatCore(void);
