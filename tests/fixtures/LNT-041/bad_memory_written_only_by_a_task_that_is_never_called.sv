// sverity-args: --top top
// No call reaches w, so its constant-index writes build no hardware: mem is never driven.
// expect: LNT-008 WARNING 6
// expect-none: LNT-041
module top(input logic clk, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [2];
  task automatic w(input logic [7:0] d); mem[0] <= d; mem[1] <= ~d; endtask
  assign rd = mem[0] ^ mem[1];
endmodule
