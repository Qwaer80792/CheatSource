// Приведенный ниже блок ifdef — это стандартный метод создания макросов, упрощающий процедуру
// экспорта из библиотек DLL. Все файлы данной DLL скомпилированы с использованием символа INTERNALLOADER_EXPORTS
// Символ, определенный в командной строке. Этот символ не должен быть определен в каком-либо проекте,
// использующем данную DLL. Благодаря этому любой другой проект, исходные файлы которого включают данный файл, видит
// функции INTERNALLOADER_API как импортированные из DLL, тогда как данная DLL видит символы,
// определяемые данным макросом, как экспортированные.
#ifdef INTERNALLOADER_EXPORTS
#define INTERNALLOADER_API __declspec(dllexport)
#else
#define INTERNALLOADER_API __declspec(dllimport)
#endif

// Этот класс экспортирован из библиотеки DLL
class INTERNALLOADER_API CInternalLoader {
public:
	CInternalLoader(void);
	// TODO: добавьте сюда свои методы.
};

extern INTERNALLOADER_API int nInternalLoader;

INTERNALLOADER_API int fnInternalLoader(void);
