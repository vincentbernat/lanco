lanĉo
=====

A more complete presentation can be found on this [blog post][].

[blog post]: http://vincent.bernat.im/en/blog/2013-lanco.html

Features
--------

`lanco` is a task launcher which does not want to be an init program.

 - No daemon.
 - Run each task in a dedicated cgroup.
 - Log the output of the task to a file.
 - Check if a task is still running.
 - Stop a task.
 - No need to be root.

Each task is given a name and enclosed into a cgroup. The cgroup is
used to keep track of the task. If the cgroup still exists, the task
is considered to be running. Stopping a task is equivalent to killing
the tasks in the cgroup.

Usage
-----

The usage is described in the `lanco.8` manual page.
