set terminal postscript eps color "Sans-serif" 16
set output 'plot.eps'
set key right bottom
set xlabel "Nombre de noeuds dans la bissection"
set ylabel "Debit en Mo/s"
set grid

# Appel extérieur à epstopdf pour transformer le fichier eps en pdf.
plot 'Result/gdx/flowsSumsBisTCPpas5.dat' using 1:2 title 'gdx' with linespoints, 'Result/paradent/flowsSumsBisTCPpas5.dat' using 1:2 title 'paradent' with linespoints, 'Result/paramount/flowsSumsBisTPCpas5.dat' using 1:2 title 'paramount' with linespoints, 'Result/parapide/flowsSumsBisTCPpas5.dat' using 1:2 title 'parapide' with linespoints, 'Result/paraquad/flowsSumsBisTCPpas5.dat' using 1:2 title 'paraquad' with linespoints

!epstopdf --outfile=plot.pdf plot.eps 
quit
