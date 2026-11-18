// sverity-args: --top top
// expect-none: LNT-078
module top(input logic d, output logic y);
if (0) begin : inactive
logic value;
always_comb begin logic value; value = d; end
end
assign y = d;
endmodule
