module top(input logic gate,
  rst_n,
  input logic [7:0] value,
  
           output logic [7:0] held);

  // Deliberate transparent sample-and-hold stage.
  always_latch begin
    if (!rst_n) held <= '0;
    else if (gate) held <= value;
  end

endmodule
