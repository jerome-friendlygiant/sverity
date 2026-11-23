// sverity-args: --top top --goal cdc --sdc @DIR@/generated_launch_unknown.sdc
// expect-unresolved: CST-004
// expect-unresolved: CDC-029
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, e, output logic q, output logic div_q);
  always_ff @(posedge clk) begin
    div_q <= ~div_q;
    q <= e;
  end
endmodule
