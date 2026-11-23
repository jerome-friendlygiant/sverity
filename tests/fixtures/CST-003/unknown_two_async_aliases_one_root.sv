// sverity-args: --top top --sdc @DIR@/two_async_aliases.sdc
// Two asynchronous SDC clock aliases on one RTL clock root must never pass silently.
// expect: CST-003 WARNING
// expect-unresolved: CST-003
// expect-coverage: constraints INCOMPLETE
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
