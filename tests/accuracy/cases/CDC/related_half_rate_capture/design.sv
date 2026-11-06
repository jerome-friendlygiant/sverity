module top(input logic src_clk,
  dst_clk,
  input logic [7:0] din,
  
           output logic [7:0] dout);

  logic [7:0] launched;
  always_ff @(posedge src_clk) begin
    launched <= din;
  end
  always_ff @(posedge dst_clk) begin
    dout <= launched;
  end

endmodule
