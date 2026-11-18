// sverity-args: --top top
// A memory inside a module-level struct is written from two processes; the nested memory path cannot be attributed to a word.
// expect: LNT-049 WARNING 6
module top(input logic clk, d, input logic [1:0] a, output logic q);
  typedef struct {logic mem[0:3];} S; S s;
  always @(posedge clk) s.mem[a] <= d;
  always @(negedge clk) s.mem[a] <= ~d;
  assign q = s.mem[0];
endmodule
