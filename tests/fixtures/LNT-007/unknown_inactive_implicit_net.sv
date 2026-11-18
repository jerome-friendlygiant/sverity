// sverity-args: --top top
// expect-none: LNT-007
module top(input logic d, output logic y);
if (0) begin : inactive
assign typo = d;
end
assign y = d;
endmodule
