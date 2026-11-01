// sverity-args: --top top
// expect: LNT-056 WARNING
module top(input logic [3:0] a, output logic [7:0] y);
  assign y = {a, 5};
endmodule
