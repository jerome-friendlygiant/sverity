// sverity-args: --top top
// A `default: '0` fills every element whatever its width, and `'{default: 1'b0}` into one-bit elements loses nothing, even when the array length is a conditional.
// expect-none: LNT-057
module top #(parameter int W = 4)(input logic clk, output logic [W-1:0] q [3]);
  function automatic bit check_static();
    bit unused_bit [((W > 1) ? 1 : -1)];
    unused_bit = '{default: 1'b0};
    return unused_bit[0];
  endfunction
  localparam bit Checked = check_static();
  always_ff @(posedge clk) q <= '{default: '0};
endmodule
