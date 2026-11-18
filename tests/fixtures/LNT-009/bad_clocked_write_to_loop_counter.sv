// sverity-args: --top top
// A nonblocking write in a clocked procedure builds a flop that drives `j`, whatever the combinational loop does with it: two drivers.
// expect: LNT-009 ERROR 7
module top(input logic c, input logic [7:0] a, output logic [7:0] y);
  integer j;
  always @(posedge c) j <= 3;
  always @* for (j = 0; j < 8; j = j + 1) y[j] = a[j];
endmodule
