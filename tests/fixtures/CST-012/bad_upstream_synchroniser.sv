// sverity-args: --top top --sdc @DIR@/async.sdc
// expect: CST-012 ERROR
// revert: posedge clk_b => posedge clk_a
// revert-expect-none: CST-012 ERROR
module top(input logic clk_a, clk_b, d, output logic q);
 logic s1;
 always_ff @(posedge clk_b) begin s1 <= d; q <= s1; end
endmodule
