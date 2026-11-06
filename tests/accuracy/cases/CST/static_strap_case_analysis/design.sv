module top(input logic clk,
  boot_mode,
  input logic [7:0] din,
  
           output logic [7:0] dout);

  // Board strap boot_mode is permanently tied low in this product.
  logic [7:0] routed;
  assign routed = boot_mode ? ~din : din;
  always_ff @(posedge clk) begin
    dout <= routed;
  end

endmodule
