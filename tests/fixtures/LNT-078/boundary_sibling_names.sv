// sverity-args: --top top
// expect-none: LNT-078
module top(input logic a,b, output logic q,r);
always_comb begin : left_block logic local_value; local_value = a; q = local_value; end
always_comb begin : right_block logic local_value; local_value = b; r = local_value; end
endmodule
