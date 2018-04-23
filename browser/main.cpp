#include "main_window.h"
#include <QApplication>

int main(int argc, char *argv[])
{
  QApplication a(argc, argv);

  a.setApplicationName( "CS Database Browser (v1)" );
  a.setOrganizationName( "CREDITS" );

  MainWindow w;
  w.show();

  return a.exec();
}
