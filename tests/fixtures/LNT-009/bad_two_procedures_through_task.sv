// sverity-args: --top top
// revert: always @(posedge clk) write_q(b); => always @(posedge clk) r <= b;
// revert-expect-none: LNT-009
// Both procedures write q through the same task: two drivers, made inside the task.
// expect: LNT-009 WARNING
module top(input logic clk, a, b, output logic q, r);
  task write_q(input logic v); q <= v; endtask
  always @(posedge clk) write_q(a);
  always @(posedge clk) write_q(b);
endmodule
