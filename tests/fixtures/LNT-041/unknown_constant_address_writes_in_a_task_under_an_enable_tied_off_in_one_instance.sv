// sverity-args: --top top
// The only call of w sits under an enable u0 leaves free and u1 ties to 0: the memory of u1 is never driven, which the shared body cannot say.
// expect-unresolved: LNT-041 5
module child(input logic clk, we, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [2];
  task automatic w(input logic [7:0] d); mem[0] <= d; mem[1] <= ~d; endtask
  always_ff @(posedge clk) if (we) w(wd);
  assign rd = mem[0] ^ mem[1];
endmodule
module top(input logic clk, we, input logic [7:0] wd, output logic [7:0] r0, r1);
  child u0(.clk, .we, .wd, .rd(r0));
  child u1(.clk, .we(1'b0), .wd, .rd(r1));
endmodule
