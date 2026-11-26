// sverity-args: --top top --sdc @DIR@/to_only.sdc
// The exception names rst_n only in its to list, the last exempt shape: the rule's trigger needs the reset in the from or through list, so no finding.
// expect-none: RDC-027
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
