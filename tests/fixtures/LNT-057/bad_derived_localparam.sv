// sverity-args: --top top
// expect: LNT-057 WARNING 7
module top #(parameter W = 8) (input logic [7:0] a, output logic [3:0] o);
  localparam N = W;
  logic [N-1:0] value;
  assign value = a;
  assign o = value;
endmodule
