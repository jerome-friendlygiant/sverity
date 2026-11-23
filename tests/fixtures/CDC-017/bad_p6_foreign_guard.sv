// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-017 ERROR
module top(input logic clka, clkb, rsta, inc, sclr, output logic [3:0] o);
  logic [3:0] bin, gray, s1, s2, bnext, gnext; logic m_rst_sync3_reg;
  always_ff @(posedge clka) m_rst_sync3_reg <= sclr;
  assign bnext = inc ? bin + 4'd1 : bin;
  assign gnext = bnext ^ (bnext >> 1);
  always_ff @(posedge clka or posedge rsta) begin
    if (rsta) begin bin <= 0; gray <= 0; end
    else begin bin <= bnext; gray <= gnext; end
  end
  always_ff @(posedge clkb) begin
    s1 <= gray; s2 <= s1;
    if (m_rst_sync3_reg) s1 <= 0;
  end
  assign o = s2;
endmodule
