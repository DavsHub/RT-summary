# RT-summary
Summary para la asignatura RT


Enunciado:
# Summary: CHATin - 10/04/2025
Queremos implementar un servicio de chat (aunque no muy relacionado con RT, si que es útil para
repasar todos los servicios del sistema operativo explicados hasta ahora). El servicio está compuesto por
2 componentes: un servidor y un cliente.
## **Cliente**
Un cliente se conectará a un servidor de chat indicando, en la línea de comandos, el nombre de usuario
y los puertos (uno de envió de mensajes y otro de recepción de mensajes) a los que se tiene que
conectar:\
\> client username serverip port-in port-out\
El cliente, al ejecutarse, creará dos threads, uno para escribir mensajes y otro para leerlos. El thread que
escribe mensajes, es el que se conectará a serverip:port-in y, al inicio de la conexión, enviará el
username al servidor. Después entrará en un bucle donde leerá, mediante getchar(), caracteres del
teclado y cuando detecte un ‘\n’, enviará todos los caracteres que ha leído desde el último ‘\n’ (o desde
el principio de su ejecución si no ha habido un ‘\n’ anterior).\
El thread de lectura se conectará mediante un socket al puerto serverip:port-out, enviará el username
para que el server sepa que se refiere al mismo cliente y entrará en un bucle donde lo único que hará
será leer mensajes y mostrarlos por pantalla.\
El cliente acabará cuando reciba el signal SIGUSR1.
## **Servidor**
El servidor siempre se ejecutará pasándole el port-in y port-out donde recibirá y enviará mensajes:
\> server port-in port-out\
Cuando el server se ejecute, tendrá un único thread que abra los dos puertos (port-in, port-out) y se
espere a que los clientes se conecten. Cuando un cliente se conecte, guardará en un vector global al
proceso, los file descriptors de las conexiones de ese cliente, junto con el username que le envía el
cliente. Este vector global tendrá espacio para, como mucho, 100 clientes. Además, creará un nuevo
thread (secundario) que atenderá a ese cliente.\
El funcionamiento de estos threads secundarios es siempre el mismo: se esperarán a que les llegue un
mensaje por parte del cliente, del cual atienden peticiones, y reenvían este mensaje al resto de clientes
(excepto a él mismo) incluyendo el username del cliente que está enviando el mensaje. Cuando un
cliente acabe la comunicación, el thread marcará que este cliente se ha desconectado y acabará.
Ten en cuenta que este vector global donde guardas la información de los clientes tiene que accederse
en exclusión mutua.\
El server acabará cuando reciba el signal SIGUSR2.

A tener en cuenta:
- los mensajes que se envían siempre son mensajes de texto que no tienen longitud máxima. Por esta
razón, para enviar cualquier mensaje de texto (el username o un mensaje de texto) se tiene que enviar
siempre la longitud, en caracteres, de este mensaje antes del propio mensaje. Además, para evitar
condiciones de carrera, estos mensajes (longitud + texto) se tienen que enviar de forma atómica
