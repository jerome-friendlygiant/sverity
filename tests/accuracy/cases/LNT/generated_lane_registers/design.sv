module top #(parameter int LANES=3)(input logic clk,
  rst_n,
  
  input logic [LANES-1:0][7:0] din,
  output logic [LANES-1:0][7:0] dout);

  for (genvar k=0; k<LANES; k++) begin : lanes
    always_ff @(posedge clk) begin
      if (!rst_n) dout[k] <= '0;
      else dout[k] <= din[k];
    end
  end

endmodule
