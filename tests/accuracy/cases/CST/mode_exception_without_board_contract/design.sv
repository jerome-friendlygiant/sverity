module top(input logic clk,
  mode,
  input logic [7:0] normal_data,
  service_data,
  
           output logic [7:0] dout);

  logic [7:0] chosen;
  always_comb begin
    chosen = mode ? service_data : normal_data;
  end
  always_ff @(posedge clk) begin
    dout <= chosen;
  end

endmodule
