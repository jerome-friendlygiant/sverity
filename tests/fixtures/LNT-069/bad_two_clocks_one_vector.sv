// sverity-args: --top top
// Two bits of one vector are two controls; the block uses neither.
// expect: LNT-069 ERROR 5
module top(input logic [1:0] c, input logic d, output logic q);
  always_ff @(posedge c[0] or posedge c[1])
    q <= d;
endmodule
