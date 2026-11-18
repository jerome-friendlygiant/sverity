// sverity-args: --top top
// expect: LNT-082 INFO
// expect-none: LNT-082 WARNING
module top(input logic [1:0] a, output logic [7:0] q);
  logic [7:0] mem[4];
  initial begin mem[0]=0; mem[1]=1; mem[2]=2; mem[3]=3; end
  assign q=mem[a];
endmodule
