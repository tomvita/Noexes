This fork modifies the sysmodule to make use of dmnt:cht services instead of reporting an error when the dmnt:cht is already attached to the game process.

Launching of app that force load dmnt:cht will still crash the switch if noexs is attached to the game process. No solution for this one yet. 

To install the sysmodule copy the content of 054e4f4558454000.zip to contents directory of atmosphere.
If you have 0100000000000038 from older version you must remove it for the new version to work.

## License

This project is licensed under GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details



