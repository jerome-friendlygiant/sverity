// sverity-args: --top top --sdc @DIR@/fp.sdc
// A set_false_path whose -from names only a synchroniser-only reset input is the standard constraint and exempt from RDC-027.
// expect-none: RDC-027
module top(input logic clka, rst_n, d, output logic q);
  logic [1:0] sr;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) sr <= 2'b00;
    else sr <= {sr[0], 1'b1};
  always_ff @(posedge clka or negedge sr[1])
    if (!sr[1]) q <= 1'b0;
    else q <= d;
endmodule
