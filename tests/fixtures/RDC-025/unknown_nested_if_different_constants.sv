// sverity-args: --top top
// Bad lookalike: nested if/else arms in an async reset branch write different constants to bit 0, so the forced value is undecided.
// expect-unresolved: RDC-025
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clk, rst_n, mode, input logic [1:0] d, output logic [1:0] q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      if (mode) q <= 2'b00;
      else q <= 2'b01;
    end else begin
      q <= d;
    end
endmodule
