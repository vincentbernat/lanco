lanĉo
=====

*Warning*: currently, `lanco` only works with cgroups v1. It could be
adapted for cgroups v2 but I don't use it anymore and nobody asked
about it either.

A more complete presentation can be found on this [blog post][].

[blog post]: https://vincent.bernat.ch/en/blog/2013-lanco

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

Name
----

lanĉo means "launcher" in Esperanto. It should have been spelled
"lancxo" without the diacritic but I have learned about it too late.
