# Minishell

Repositorio para el código de la práctica 1 de SSOO.

## Cómo descargar el código
### Requisitos

- [Tener Git instalado](https://git-scm.com/downloads) (en Linux, ya viene por defecto: ``git --version``)
- [Tener un compilador de C instalado](https://gcc.gnu.org/) (en Linux podría venir por defecto: ``gcc --version``)

### Cómo ejecutar el código

- Clona el código de GitHub en una carpeta de tu gusto: ``git clone https://github.com/Daniel-Barbera/MinishellSSOO.git``
- Una vez en la carpeta del repositorio (``cd MinishellSSOO``), compila el código. ``gcc myshell.c libparser_64.a -o msh.out``. En Windows, debes usar ``libparser.a`` en lugar de ``libparser_64.a``.
- Ejecuta el código: ``./msh.out``

### Cómo editar el código

Nota: si no quieres aprenderte los comandos o usar la terminal, siempre puedes usar [Visual Studio Code](https://code.visualstudio.com/).<br>
Tutorial para usar Git en VSC: https://www.youtube.com/watch?v=i_23KUAEtUM

- Asegúrate de que tienes los últimos cambios de GitHub. (``git pull``)
- Asegúrate de que estás en la rama correcta. (``git checkout mi-rama``)
- Haz el cambio.
- Cuando estés lista para subir tus cambios, ``git add .`` desde ``MinishellSSOO`` añade todos los ficheros nuevos, y ``git commit . -m "Mi mensaje"`` guarda tus cambios localmente, junto con un mensaje para que recuerdes qué hiciste en ese commit.
- ``git push`` sube tus cambios a GitHub.
