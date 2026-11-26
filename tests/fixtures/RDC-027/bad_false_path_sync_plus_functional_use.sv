// sverity-args: --top top --sdc @DIR@/fp.sdc
// A set_false_path on a reset input that feeds a reset synchroniser AND also has a functional use (synchronous reset or data) must still report RDC-027 WARNING.
// revert: if (!rst_n) p <= 1'b0; => if (!sr[1]) p <= 1'b0;
// revert-expect-none: RDC-027 WARNING
// expect: RDC-027 WARNING
module top(input logic clka, rst_n, d, output logic q, p);
  logic [1:0] sr;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) sr <= 2'b00;
    else sr <= {sr[0], 1'b1};
  always_ff @(posedge clka or negedge sr[1])
    if (!sr[1]) q <= 1'b0;
    else q <= d;
  always_ff @(posedge clka)
    if (!rst_n) p <= 1'b0;
    else p <= d;
endmodule
