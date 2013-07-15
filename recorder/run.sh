#!/bin/sh
make && sudo make install && sudo /etc/init.d/recordthepiano stop && sudo /etc/init.d/recordthepiano start && tail -f -n 10000 /var/log/recordthepiano/recordthepiano.log
