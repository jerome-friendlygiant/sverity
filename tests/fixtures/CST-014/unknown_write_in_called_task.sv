// sverity-args: --top top --sdc @DIR@/unknown_write_in_called_task.sdc
// A task called from an initial block sets m, which is no more undriven than a direct write.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, output logic q);
  logic m;
  task set_mode; m = 1'b1; endtask
  initial set_mode();
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
