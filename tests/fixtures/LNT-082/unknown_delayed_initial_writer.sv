// sverity-args: --top top
// expect: LNT-082 WARNING
module top(input logic [1:0] a, output logic [7:0] q);
  logic [7:0] mem[4];
  initial begin #10; mem[0]=1; end
  assign q=mem[a];
endmodule
