// sverity-args: --top top
// revert: always @(posedge clk) if (clear) busy <= 1'b0; => always @(posedge clk) if (clear) idle <= 1'b0;
// revert-expect-none: LNT-009
// Two plain always procedures write the same register: two drivers.
// expect: LNT-009 WARNING 8
module top(input logic clk, launch, clear, output logic busy, idle);
  always @(posedge clk) if (launch) busy <= 1'b1;
  always @(posedge clk) if (clear) busy <= 1'b0;
endmodule
