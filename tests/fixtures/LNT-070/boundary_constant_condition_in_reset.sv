// sverity-args: --top top
// An if inside the reset branch whose condition is a constant does not choose the reset value at run time.
// expect-none: LNT-070
module top #(parameter bit WIDE = 1'b1) (input logic clk, rst_n, d, output logic [1:0] q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      if (WIDE) q <= 2'b11;
      else q <= 2'b00;
    end
    else q <= {d, d};
endmodule
