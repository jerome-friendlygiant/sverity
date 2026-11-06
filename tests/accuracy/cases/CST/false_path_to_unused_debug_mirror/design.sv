module top(input logic clk,
  input logic [7:0] din,
  
           output logic [7:0] dout,
  output wire [7:0] debug_copy);

  // debug_copy is observed asynchronously by a diagnostic probe only.
  assign debug_copy = din;
  always_ff @(posedge clk) begin
    dout <= din;
  end

endmodule
