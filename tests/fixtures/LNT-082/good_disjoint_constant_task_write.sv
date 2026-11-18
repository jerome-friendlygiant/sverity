// A write hidden in a task is attributed to its constant word, so a read of another word cannot collide.
// sverity-args: --top top
// expect-none: LNT-082 WARNING
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  task automatic put(input logic [7:0] v);
    mem[0] = v;
  endtask
  always_ff @(posedge clk) put(d);
  assign q = mem[1];
endmodule
