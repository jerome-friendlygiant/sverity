// sverity-args: --top top
// expect: LNT-049 INFO 5
// expect-none: LNT-049 WARNING
module top(input logic [7:0] d, output logic [7:0] q);
logic [7:0] bank [2];
always_comb begin bank[0] = d; bank[1] = ~d; end
assign q = bank[1];
endmodule
