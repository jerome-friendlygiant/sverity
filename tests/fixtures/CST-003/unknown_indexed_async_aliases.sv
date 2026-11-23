// sverity-args: --top top --sdc @DIR@/indexed_async_aliases.sdc
// Two asynchronous -add SDC aliases on an indexed clock port bit clk[0] must trigger CST-003 WARNING Unresolved.
// expect: CST-003 WARNING
// expect-unresolved: CST-003
// expect-coverage: constraints INCOMPLETE
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic [1:0] clk, input logic d, output logic q);
  always_ff @(posedge clk[0]) q <= d;
endmodule
