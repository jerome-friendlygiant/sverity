// sverity-args: --top top
// expect-none: LNT-057
module top (input logic [7:0] a, output logic [3:0] o);
  localparam N = 8;
  logic [N-1:0] value;
  assign value = a;
  assign o = value;
endmodule
