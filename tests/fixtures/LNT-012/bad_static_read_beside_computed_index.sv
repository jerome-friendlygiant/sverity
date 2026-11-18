// sverity-args: --top top
// y reads a[0] for certain, beside a computed index that could reach any bit: the loop through a[0] exists whatever i is.
// expect: LNT-012 ERROR
module top(input logic [1:0] i, output logic y);
  logic [3:0] a;
  assign y = a[i] | a[0];
  assign a = {3'b0, y};
endmodule
