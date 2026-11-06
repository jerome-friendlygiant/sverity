(* black_box *) module pll(input wire ref_clk,
  output wire fast_clk);


endmodule
module top(input wire clk,
  input logic [7:0] din,
  output logic [7:0] dout);

  wire fast_clk;
  pll u_pll(.ref_clk(clk), .fast_clk(fast_clk));
  always_ff @(posedge fast_clk) begin
    dout <= din;
  end

endmodule
